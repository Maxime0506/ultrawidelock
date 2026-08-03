/**
 * @file
 * @brief Applies a staged delta patch onto the primary slot, from inside
 *        MCUboot.
 *
 * Runs as a SYS_INIT at APPLICATION level. That level is chosen, not
 * convenient: it is after the flash driver has initialised (POST_KERNEL) and
 * before MCUboot's own main(), which is the only window in which the primary
 * slot can be rewritten. It also means NOT ONE LINE of fetched upstream MCUboot
 * is edited -- the bootloader loads this the same way it loads every other
 * Zephyr module.
 *
 * Why this cannot live in the application: the application executes from the
 * primary slot. Rewriting it would be rewriting the code doing the rewriting.
 *
 * On a normal boot this costs one word read: the header magic does not match
 * and the function returns immediately.
 *
 * SAFETY. Three things stand between a bad patch and a dead lock, and only the
 * third is load-bearing:
 *   1. the header carries a CRC of itself, written last, so a torn write fails
 *   2. the patch and the from-image are CRC-checked before a byte is erased
 *   3. MCUboot re-verifies the P-256 signature of the RESULT before booting it
 *      (CONFIG_BOOT_VALIDATE_SLOT0=y), and drops to serial recovery if it
 *      fails (CONFIG_BOOT_SERIAL_NO_APPLICATION=y)
 * So the worst a corrupt or forged patch achieves is destroying the installed
 * image, which is recoverable, rather than installing code, which is not.
 *
 * POWER CUTS ARE EXPECTED, not exceptional: this rewrites most of 442 KB and
 * takes seconds. detools' step counter is what makes that survivable -- see
 * step_set()/step_get() below.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include <pm_config.h>

#include "woz_dfu.h"
#include "detools.h"

#if defined(CONFIG_WOZ_DFU_APPLIER_LOG)
#include <zephyr/sys/printk.h>
#define DFU_LOG(...) printk("WDFU " __VA_ARGS__)
#else
#define DFU_LOG(...) ((void)0)
#endif

/**
 * The region the patch transforms: MCUboot's primary slot, header and all.
 *
 * Not mcuboot_primary_app. The patch is generated between two SIGNED images,
 * because the signature and its TLVs are part of what has to change -- patching
 * only the payload would leave the old signature in front of new code and
 * MCUboot would refuse to boot it.
 */
#define PRIMARY_ID PM_MCUBOOT_PRIMARY_ID
#define STAGING_ID PM_PATCH_STAGING_ID

/** Largest patch the staging partition can hold after the two control pages. */
#define PATCH_MAX (PM_PATCH_STAGING_SIZE - WOZ_DFU_PATCH_OFFSET)

/** Most steps the one-page step log can record, one word each. */
#define STEP_MAX (WOZ_DFU_PAGE_SIZE / sizeof(uint32_t))

/* The host builds this header with struct.pack and this side reads it with a
 * flash_area_read straight into the struct, so the two agree on the layout or
 * nothing works. Caught here rather than on the bench. */
BUILD_ASSERT(sizeof(struct woz_dfu_hdr) == WOZ_DFU_HDR_LEN,
	     "woz_dfu_hdr changed size; scripts/woz_patch.py must change with it");
BUILD_ASSERT(WOZ_DFU_HDR_CRC_LEN == WOZ_DFU_HDR_LEN - sizeof(uint32_t),
	     "the header CRC must cover everything except itself");

/**
 * Context passed to detools patch applier callbacks holding the primary and staging flash areas.
 */
struct apply_ctx {
	const struct flash_area *primary;
	const struct flash_area *staging;
};

/* Static, not automatic: this runs on the main thread's stack and the patcher
 * state is a few hundred bytes. MCUboot's stack has an ECDSA verify to do
 * afterwards. */
static struct apply_ctx s_ctx;
static struct detools_apply_patch_in_place_t s_patcher;
static uint8_t s_chunk[CONFIG_WOZ_DFU_APPLIER_CHUNK];

/* ---- detools memory callbacks -------------------------------------------- */
/*
 * Addresses are offsets within the primary slot, 0-based, which is exactly what
 * flash_area_* takes.
 *
 * WRITES ARE COMBINED, and that is not an optimisation. detools writes whatever
 * its decompressor produced -- `to_size = MIN(sizeof(to), chunk_size)` at
 * detools.c:1936 -- so both the address and the length are arbitrary. nRF flash
 * has a 4-byte write block, so handing those straight to flash_area_write()
 * fails on any chunk that is not word-aligned and word-sized, which is most of
 * them. The shift loop earlier in the patch (detools.c:1690-1711) happens to
 * use 128-byte aligned chunks and works either way; the patch phase does not.
 *
 * So bytes accumulate here and only whole words go to flash. A partial tail is
 * held until either the next contiguous write completes the word, or something
 * proves the region is finished -- a write somewhere else, a read, an erase, or
 * the end of the patch -- at which point it is padded with 0xff. Padding is
 * correct there precisely because the region IS finished: detools has moved on,
 * and the surrounding flash was left erased.
 */

/** Word-aligned staging for the write combiner. 128 matches detools' own. */
#define WBUF_SZ 128

static struct {
	off_t addr; /**< primary-slot offset that buf[0] belongs at */
	size_t len; /**< bytes currently held */
	uint8_t buf[WBUF_SZ];
} s_w;

/** Push out every complete word we are holding. */
static int wbuf_flush_words(struct apply_ctx *c)
{
	size_t whole = s_w.len & ~(size_t)3;

	if (whole == 0U) {
		return 0;
	}
	if (flash_area_write(c->primary, s_w.addr, s_w.buf, whole) != 0) {
		DFU_LOG("w %u+%u failed\n", (unsigned)s_w.addr, (unsigned)whole);
		return -1;
	}

	s_w.len -= whole;
	s_w.addr += (off_t)whole;
	if (s_w.len > 0U) {
		memmove(s_w.buf, s_w.buf + whole, s_w.len);
	}
	return 0;
}

/** Push out everything, padding a partial tail to a word with erased bits. */
static int wbuf_flush_all(struct apply_ctx *c)
{
	if (wbuf_flush_words(c) != 0) {
		return -1;
	}
	if (s_w.len == 0U) {
		return 0;
	}

	while (s_w.len & 3U) {
		s_w.buf[s_w.len++] = 0xff;
	}
	if (flash_area_write(c->primary, s_w.addr, s_w.buf, s_w.len) != 0) {
		DFU_LOG("wt %u+%u failed\n", (unsigned)s_w.addr, (unsigned)s_w.len);
		return -1;
	}

	s_w.addr += (off_t)s_w.len;
	s_w.len = 0U;
	return 0;
}

/**
 * Write bytes to the primary flash area during patch application by buffering them and flushing
 * when the buffer reaches word size or on a discontiguous write; returns -1 on failure.
 */
static int mem_write(void *arg_p, uintptr_t dst, void *src_p, size_t size)
{
	struct apply_ctx *c = arg_p;
	const uint8_t *src = src_p;

	if (s_w.len > 0U && (off_t)dst != s_w.addr + (off_t)s_w.len) {
		if (wbuf_flush_all(c) != 0) {
			return -1;
		}
	}
	if (s_w.len == 0U) {
		s_w.addr = (off_t)dst;
	}

	while (size > 0U) {
		size_t n = MIN(size, WBUF_SZ - s_w.len);

		memcpy(s_w.buf + s_w.len, src, n);
		s_w.len += n;
		src += n;
		size -= n;

		if (s_w.len == WBUF_SZ && wbuf_flush_words(c) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Read bytes from the primary flash area for a patch segment by flushing any buffered writes first
 * and reading from the flash area; returns -1 on failure.
 */
static int mem_read(void *arg_p, void *dst_p, uintptr_t src, size_t size)
{
	struct apply_ctx *c = arg_p;

	/* Anything still buffered has to be in flash before it can be read back. */
	if (wbuf_flush_all(c) != 0) {
		return -1;
	}
	if (flash_area_read(c->primary, (off_t)src, dst_p, size) != 0) {
		DFU_LOG("r %u+%u failed\n", (unsigned)src, (unsigned)size);
		return -1;
	}
	return 0;
}

/**
 * Erase flash pages for a patch segment by rounding the size up to page boundaries, validating
 * alignment and bounds, and flushing any buffered writes first; returns -1 on failure.
 */
static int mem_erase(void *arg_p, uintptr_t addr, size_t size)
{
	struct apply_ctx *c = arg_p;
	size_t rounded;

	if (wbuf_flush_all(c) != 0) {
		return -1;
	}

	/*
	 * detools erases exactly the bytes a segment covers, and the LAST
	 * segment is a partial one -- 471 bytes for this image. Flash erases
	 * pages, so that call fails outright.
	 *
	 * Rounding UP is safe, and not by luck. The forward shift is two
	 * segments, so the from-data segment k reads has been moved to start at
	 * exactly (k+1) * segment_size -- the first byte past the page this
	 * erase rounds up to. That is the property the shift exists to provide,
	 * and it is why the shift must stay a multiple of the segment size.
	 *
	 * Rounding the START down is never safe -- it would erase to-data
	 * already written by an earlier segment -- so an unaligned start is a
	 * hard failure rather than something to paper over.
	 */
	if ((addr % WOZ_DFU_PAGE_SIZE) != 0U) {
		DFU_LOG("e %u unaligned\n", (unsigned)addr);
		return -1;
	}

	rounded = ROUND_UP(size, WOZ_DFU_PAGE_SIZE);
	if (addr + rounded > (uintptr_t)c->primary->fa_size) {
		DFU_LOG("e %u+%u past slot\n", (unsigned)addr, (unsigned)rounded);
		return -1;
	}

	if (flash_area_erase(c->primary, (off_t)addr, rounded) != 0) {
		DFU_LOG("e %u+%u failed\n", (unsigned)addr, (unsigned)rounded);
		return -1;
	}
	return 0;
}

/* ---- the step log -------------------------------------------------------- */
/*
 * detools calls step_set() after each completed segment and step_get() when it
 * wants to know where to resume. Steps start at 1 and increase by one, so step
 * N is recorded as a word at index N-1 and the number of written words IS the
 * last completed step.
 *
 * Append-only, never erased mid-apply: each word is written exactly once
 * between erases, which is the write pattern nRF flash is happiest with.
 */

/**
 * Record or clear the patch application step counter in the staging area: step 0 erases the page,
 * positive steps append a word, and out-of-range or negative steps fail; returns -1 on error.
 */
static int step_set(void *arg_p, int step)
{
	struct apply_ctx *c = arg_p;
	uint32_t value = (uint32_t)step;

	/*
	 * STEP 0 IS NOT A STEP. detools calls step_set(0) to CLEAR the counter,
	 * once every step is done -- in_place_all_steps_completed(),
	 * detools.c:1529-1541. The log is a page of append-only words, so
	 * clearing it means erasing that page.
	 *
	 * Rejecting it, which an earlier version of this file did, fails the
	 * patch at its very last moment, after all the work succeeded, and
	 * reports it as -DETOOLS_STEP_SET_FAILED rather than as anything that
	 * points here.
	 */
	if (step == 0) {
		if (flash_area_erase(c->staging, (off_t)WOZ_DFU_STEP_OFFSET, WOZ_DFU_PAGE_SIZE) !=
		    0) {
			DFU_LOG("step clear failed\n");
			return -1;
		}
		return 0;
	}

	if (step < 0 || (size_t)step > STEP_MAX) {
		DFU_LOG("step %d out of range\n", step);
		return -1;
	}

	return flash_area_write(c->staging,
				(off_t)WOZ_DFU_STEP_OFFSET +
					((off_t)step - 1) * (off_t)sizeof(value),
				&value, sizeof(value)) == 0
		       ? 0
		       : -1;
}

/**
 * Retrieve the current step counter from the staging area by reading append-only words until an
 * erased value is found; returns 0 on success.
 */
static int step_get(void *arg_p, int *step_p)
{
	struct apply_ctx *c = arg_p;
	uint32_t value;
	size_t i;

	for (i = 0; i < STEP_MAX; i++) {
		if (flash_area_read(c->staging,
				    (off_t)WOZ_DFU_STEP_OFFSET + (off_t)i * (off_t)sizeof(value),
				    &value, sizeof(value)) != 0) {
			return -1;
		}
		if (value == WOZ_DFU_STEP_ERASED) {
			break;
		}
	}

	*step_p = (int)i;
	return 0;
}

/* ---- integrity ----------------------------------------------------------- */

/**
 * CRC-32 over a run of a flash area.
 *
 * crc32_ieee_update() is seeded with 0 and chains across calls exactly as
 * Python's zlib.crc32(data, previous) does, which is what lets the host compute
 * the same value without either side reimplementing the other's polynomial.
 */
static int area_crc32(const struct flash_area *fa, off_t off, size_t len, uint32_t *out)
{
	uint32_t crc = 0;

	while (len > 0U) {
		size_t n = MIN(sizeof(s_chunk), len);

		if (flash_area_read(fa, off, s_chunk, n) != 0) {
			return -1;
		}
		crc = crc32_ieee_update(crc, s_chunk, n);
		off += (off_t)n;
		len -= n;
	}

	*out = crc;
	return 0;
}

/** Erase the whole staging partition, consuming the update. */
static void staging_consume(struct apply_ctx *c)
{
	(void)flash_area_erase(c->staging, 0, PM_PATCH_STAGING_SIZE);
}

/* ---- the apply ----------------------------------------------------------- */

/**
 * Stream a patch from the staging area into detools in chunks, applying it in place to the primary
 * flash area, and flushing all buffered writes before finalization; returns detools result code.
 */
static int patch_stream(struct apply_ctx *c, const struct woz_dfu_hdr *hdr)
{
	size_t left = hdr->patch_len;
	off_t off = (off_t)WOZ_DFU_PATCH_OFFSET;
	int res;

	s_w.len = 0U;
	s_w.addr = 0;

	res = detools_apply_patch_in_place_init(&s_patcher, mem_read, mem_write, mem_erase,
						step_set, step_get, hdr->patch_len, c);
	if (res != 0) {
		return res;
	}

	while (left > 0U) {
		size_t n = MIN(sizeof(s_chunk), left);

		if (flash_area_read(c->staging, off, s_chunk, n) != 0) {
			return -1;
		}

		res = detools_apply_patch_in_place_process(&s_patcher, s_chunk, n);
		if (res != 0) {
			return res;
		}

		off += (off_t)n;
		left -= n;
	}

	/* Nothing may stay in the combiner: detools has no idea it exists, so
	 * finalize() would report success over bytes still sitting in RAM. */
	if (wbuf_flush_all(c) != 0) {
		return -1;
	}

	return detools_apply_patch_in_place_finalize(&s_patcher);
}

/**
 * Apply a staged firmware delta to the primary flash partition during boot. Opens staging and
 * primary areas, validates header and patch CRC, applies patch in resumable steps, and erases
 * staging on completion or failure. Returns 0 always; actual success determined by primary image
 * validity on next boot.
 */
static int woz_dfu_apply(void)
{
	struct woz_dfu_hdr hdr;
	uint32_t crc;
	int completed = 0;
	int res;

	if (flash_area_open(STAGING_ID, &s_ctx.staging) != 0) {
		return 0;
	}

	/* The normal-boot fast path: no header, nothing staged, one read. */
	if (flash_area_read(s_ctx.staging, (off_t)WOZ_DFU_HDR_OFFSET, &hdr, sizeof(hdr)) != 0 ||
	    hdr.magic != WOZ_DFU_MAGIC) {
		flash_area_close(s_ctx.staging);
		return 0;
	}

	DFU_LOG("staged: len=%u to=%u\n", (unsigned)hdr.patch_len, (unsigned)hdr.to_len);

	if (hdr.abi_version != WOZ_DFU_ABI_VERSION || hdr.patch_len == 0U ||
	    hdr.patch_len > PATCH_MAX ||
	    hdr.hdr_crc32 != crc32_ieee((const uint8_t *)&hdr, WOZ_DFU_HDR_CRC_LEN)) {
		DFU_LOG("header rejected\n");
		staging_consume(&s_ctx);
		flash_area_close(s_ctx.staging);
		return 0;
	}

	if (flash_area_open(PRIMARY_ID, &s_ctx.primary) != 0) {
		flash_area_close(s_ctx.staging);
		return 0;
	}

	if (step_get(&s_ctx, &completed) != 0) {
		goto done;
	}

	/*
	 * The from-image check is only meaningful on a FRESH apply. Once any
	 * step has completed the primary slot is half patched by design, so it
	 * no longer hashes to from_crc32 and re-checking it would reject every
	 * resume. That is also what makes the whole thing idempotent: a patch
	 * that already finished cannot match from_crc32 either, so a power cut
	 * between the last step and the erase below cannot re-apply it.
	 */
	if (completed == 0) {
		if (hdr.from_len > (uint32_t)s_ctx.primary->fa_size ||
		    area_crc32(s_ctx.staging, (off_t)WOZ_DFU_PATCH_OFFSET, hdr.patch_len, &crc) !=
			    0 ||
		    crc != hdr.patch_crc32) {
			DFU_LOG("patch crc bad\n");
			staging_consume(&s_ctx);
			goto done;
		}

		if (area_crc32(s_ctx.primary, 0, hdr.from_len, &crc) != 0 ||
		    crc != hdr.from_crc32) {
			DFU_LOG("not for this image\n");
			staging_consume(&s_ctx);
			goto done;
		}
	}

	DFU_LOG("applying from step %d\n", completed);
	res = patch_stream(&s_ctx, &hdr);
	DFU_LOG("apply res=%d\n", res);

	/*
	 * Consume the staging partition either way. On success there is nothing
	 * left to do; on failure the primary slot is already damaged and
	 * retrying the same patch against it cannot help -- MCUboot's own
	 * validation will now fail and drop into serial recovery, which is the
	 * outcome that can actually be recovered from.
	 */
	staging_consume(&s_ctx);

done:
	flash_area_close(s_ctx.primary);
	flash_area_close(s_ctx.staging);
	return 0;
}

SYS_INIT(woz_dfu_apply, APPLICATION, 0);
