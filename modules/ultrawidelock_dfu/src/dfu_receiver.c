/* SPDX-License-Identifier: ISC */

/**
 * @file
 * @brief Application half of the delta update: receive, verify, stage, reboot.
 *
 * Never applies anything. The patch is written into `patch_staging` and the
 * board is restarted; MCUboot does the work, because the application executes
 * from the slot the patch rewrites (see src/dfu_applier.c).
 *
 * WHAT ARRIVES, in order, as one byte stream over whatever transport:
 *
 *     0   32   struct ultrawidelock_dfu_hdr
 *    32   64   ECDSA-P256 signature, raw r||s, over those 32 bytes
 *    96   ..   the patch
 *
 * The header is written to flash LAST, after the patch's CRC checks out, so a
 * cut-off transfer leaves no valid magic and the next boot ignores it. The
 * signature is checked HERE, not in the bootloader (this image already links
 * PSA ECDSA-P256; MCUboot is the flash-starved one), and the floor sits under
 * both: CONFIG_BOOT_VALIDATE_SLOT0 re-verifies the RESULT before booting, so a
 * forged header can only destroy the installed image, never install code.
 */

#include <psa/crypto.h>
#include <errno.h>
#include <string.h>

#include "dfu_crc.h"
#include "ultrawidelock_bytes.h"
#include "ultrawidelock_dfu.h"
#include "ultrawidelock_dfu_rx.h"
#include "ultrawidelock_flash.h"
#include "ultrawidelock_log.h"
#include "ultrawidelock_osal.h"

LOG_MODULE_REGISTER(ultrawidelock_dfu, CONFIG_ULTRAWIDELOCK_DFU_LOG_LEVEL);

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/** Generated at build time from the MCUboot signing key. 0x04 || X || Y. */
extern const uint8_t ultrawidelock_dfu_pubkey[65];

/** Bytes of preamble ahead of the patch: the header and its signature. */
#define HEAD_LEN (ULTRAWIDELOCK_DFU_HDR_LEN + ULTRAWIDELOCK_DFU_SIG_LEN)

/** Flash write staging. Multiple of the 4-byte write block. */
#define WBUF_SZ 64

static struct {
	bool active;
	uint32_t total; /**< wire bytes the host promised */
	uint32_t got;   /**< wire bytes consumed so far */
	uint32_t patch_crc;
	uint32_t wpos; /**< next staging offset for patch data */
	uint8_t head[HEAD_LEN];
	uint8_t wbuf[WBUF_SZ];
	size_t wlen;
} s_rx;

static const struct ultrawidelock_flash_area *s_fa;

/* Replying before rebooting is the whole reason this is deferred: a reboot
 * inside the frame handler drops the acknowledgement the host is waiting for,
 * and the host cannot then tell success from a dead board. */
static void reboot_fn(struct ultrawidelock_dwork *dwork)
{
	(void)dwork;
	LOG_INF("update staged, restarting into the bootloader");
	ultrawidelock_reboot();
}
static struct ultrawidelock_dwork s_reboot;

/* ---- window --------------------------------------------------------------- */

static void window_expire(struct ultrawidelock_dwork *dwork);
static struct ultrawidelock_dwork s_window;
static bool s_open;
static ultrawidelock_dfu_window_cb s_window_cb;

/* Both delayable items are bound on the first window call, which every route
 * into the receiver goes through -- frames and SMP uploads are refused while
 * the window has never opened. */
static void works_bind(void)
{
	static bool bound;

	if (!bound) {
		ultrawidelock_dwork_init(&s_reboot, reboot_fn);
		ultrawidelock_dwork_init(&s_window, window_expire);
		bound = true;
	}
}

/**
 * Register a callback to be invoked when the update window opens or closes.
 */
void ultrawidelock_dfu_set_window_cb(ultrawidelock_dfu_window_cb cb)
{
	s_window_cb = cb;
}

/* One place, so that every route in -- the button, Apple Home, the bench SWD
 * write -- reaches the indicator without knowing it exists. */
static void window_notify(bool open)
{
	if (s_window_cb != NULL) {
		s_window_cb(open);
	}
}

/**
 * Mark the update window closed, reset RX state, and notify all listeners (typically the UI) that
 * the window is no longer open.
 */
static void window_expire(struct ultrawidelock_dwork *dwork)
{
	(void)dwork;
	LOG_INF("update window closed");
	s_open = false;
	ultrawidelock_dfu_rx_reset();
	window_notify(false);
}

/**
 * Open the update window for the given duration in milliseconds. Reschedule the close timer and
 * notify all window listeners.
 */
void ultrawidelock_dfu_window_open(uint32_t duration_ms)
{
	works_bind();
	s_open = true;
	(void)ultrawidelock_dwork_reschedule(&s_window, (int32_t)duration_ms);
	LOG_INF("update window open for %u ms", (unsigned)duration_ms);
	window_notify(true);
}

/**
 * Cancel the update window timer, mark it closed, reset RX state, and notify all listeners that the
 * window is no longer open.
 */
void ultrawidelock_dfu_window_close(void)
{
	works_bind();
	(void)ultrawidelock_dwork_cancel(&s_window);
	s_open = false;
	ultrawidelock_dfu_rx_reset();
	window_notify(false);
}

/**
 * Return true if the update window is currently open.
 */
bool ultrawidelock_dfu_window_is_open(void)
{
	return s_open;
}

/* ---- staging flash -------------------------------------------------------- */

/**
 * Open the staging flash area if not already open. Return 0 on success or if already open; nonzero
 * on error.
 */
static int staging_open(void)
{
	if (s_fa != NULL) {
		return 0;
	}
	return ultrawidelock_flash_open(ULTRAWIDELOCK_FLASH_AREA_STAGING, &s_fa);
}

/** Room for patch bytes, after the header page and the step-log page. */
static uint32_t patch_max(void)
{
	return (uint32_t)ultrawidelock_flash_size(s_fa) - ULTRAWIDELOCK_DFU_PATCH_OFFSET;
}

/**
 * Flush buffered patch data to the staging flash area, padding to 4-byte alignment if final. Return
 * 0 on success, -1 on write error. Updates write position and shifts remaining bytes.
 */
static int wbuf_flush(bool final)
{
	size_t n = s_rx.wlen & ~(size_t)3;

	if (final) {
		while (s_rx.wlen & 3U) {
			s_rx.wbuf[s_rx.wlen++] = 0xff;
		}
		n = s_rx.wlen;
	}
	if (n == 0U) {
		return 0;
	}
	if (ultrawidelock_flash_write(s_fa, s_rx.wpos, s_rx.wbuf, n) != 0) {
		return -1;
	}

	s_rx.wpos += (uint32_t)n;
	s_rx.wlen -= n;
	if (s_rx.wlen > 0U) {
		memmove(s_rx.wbuf, s_rx.wbuf + n, s_rx.wlen);
	}
	return 0;
}

/**
 * Buffer patch data, updating the running CRC32, and flush to flash when the buffer is full. Return
 * 0 on success or nonzero on flush failure.
 */
static int patch_write(const uint8_t *data, size_t len)
{
	s_rx.patch_crc = ultrawidelock_crc32_update(s_rx.patch_crc, data, len);

	while (len > 0U) {
		size_t n = MIN(len, WBUF_SZ - s_rx.wlen);

		memcpy(s_rx.wbuf + s_rx.wlen, data, n);
		s_rx.wlen += n;
		data += n;
		len -= n;

		if (s_rx.wlen == WBUF_SZ && wbuf_flush(false) != 0) {
			return -1;
		}
	}
	return 0;
}

/* ---- authenticity --------------------------------------------------------- */

/**
 * Verify the DFU header signature using ECDSA-SHA256 with the built-in public key. Return true if
 * the signature is valid.
 */
static bool head_verifies(void)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t st;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);

	st = psa_import_key(&attr, ultrawidelock_dfu_pubkey, sizeof(ultrawidelock_dfu_pubkey), &key);
	if (st != PSA_SUCCESS) {
		LOG_ERR("pubkey import failed (%d)", (int)st);
		return false;
	}

	st = psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), s_rx.head, ULTRAWIDELOCK_DFU_HDR_LEN,
				s_rx.head + ULTRAWIDELOCK_DFU_HDR_LEN, ULTRAWIDELOCK_DFU_SIG_LEN);
	(void)psa_destroy_key(key);

	return st == PSA_SUCCESS;
}

/* ---- frame handling ------------------------------------------------------- */

/**
 * Reset the receiver state to empty: clear the RX struct.
 */
void ultrawidelock_dfu_rx_reset(void)
{
	memset(&s_rx, 0, sizeof(s_rx));
}

/**
 * Write a ULTRAWIDELOCK_DFU_RSP_OK response: set opcode to OK, append the byte count received as
 * little-endian 32-bit. Return 5 (response size).
 */
static size_t reply_ok(uint8_t *rsp)
{
	rsp[0] = ULTRAWIDELOCK_DFU_RSP_OK;
	sys_put_le32(s_rx.got, &rsp[1]);
	return 5u;
}

/**
 * Reset RX state and encode a two-byte error response with the given error code. Return the
 * response length (2 bytes).
 */
static size_t reply_err(uint8_t *rsp, enum ultrawidelock_dfu_err code)
{
	ultrawidelock_dfu_rx_reset();
	rsp[0] = ULTRAWIDELOCK_DFU_RSP_ERR;
	rsp[1] = (uint8_t)code;
	return 2u;
}

/* The three cores below carry the whole state machine, and they are shared:
 * the framed protocol wraps them in opcodes, and the SMP image group
 * (src/dfu_smp_img.c) drives the same three from CBOR. Neither transport holds
 * any transfer state of its own, so there is one place where a patch can be
 * accepted and one set of checks in front of it. ULTRAWIDELOCK_DFU_ERR_OK (0) means
 * accepted; every other value is refused. */
#define ULTRAWIDELOCK_DFU_ERR_OK ((enum ultrawidelock_dfu_err)0)

/**
 * Validate the patch size, erase the staging area including the step log, and prepare the receiver
 * to accept upload: initialize RX state with the write position at the patch offset and mark
 * reception active. Return an error code.
 */
static enum ultrawidelock_dfu_err begin_at(uint32_t total)
{
	if (staging_open() != 0) {
		return ULTRAWIDELOCK_DFU_ERR_FLASH;
	}
	if (total <= HEAD_LEN || (total - HEAD_LEN) > patch_max()) {
		return ULTRAWIDELOCK_DFU_ERR_SIZE;
	}

	/* Erase everything, including the step log: a stale one would make the
	 * bootloader believe steps of THIS patch were already applied. */
	if (ultrawidelock_flash_erase(s_fa, 0, ultrawidelock_flash_size(s_fa)) != 0) {
		return ULTRAWIDELOCK_DFU_ERR_FLASH;
	}

	ultrawidelock_dfu_rx_reset();
	s_rx.active = true;
	s_rx.total = total;
	s_rx.wpos = ULTRAWIDELOCK_DFU_PATCH_OFFSET;
	LOG_INF("update begun, %u B", (unsigned)total);
	return ULTRAWIDELOCK_DFU_ERR_OK;
}

static enum ultrawidelock_dfu_err feed_bytes(const uint8_t *p, size_t n)
{
	if (!s_rx.active) {
		return ULTRAWIDELOCK_DFU_ERR_SEQUENCE;
	}
	if (s_rx.got + n > s_rx.total) {
		return ULTRAWIDELOCK_DFU_ERR_SIZE;
	}

	/* Preamble first, then everything else is patch. */
	if (s_rx.got < HEAD_LEN) {
		size_t take = MIN(n, HEAD_LEN - s_rx.got);

		memcpy(s_rx.head + s_rx.got, p, take);
		s_rx.got += take;
		p += take;
		n -= take;

		if (s_rx.got == HEAD_LEN && !head_verifies()) {
			LOG_WRN("rejected: header signature did not verify");
			return ULTRAWIDELOCK_DFU_ERR_AUTH;
		}
	}

	if (n > 0U) {
		if (patch_write(p, n) != 0) {
			return ULTRAWIDELOCK_DFU_ERR_FLASH;
		}
		s_rx.got += n;
	}

	return ULTRAWIDELOCK_DFU_ERR_OK;
}

/* @p reboot is false only for SMP, where the host sends its own reset command
 * afterwards and a board that restarted on its own would look like a failure. */
static enum ultrawidelock_dfu_err commit_now(bool reboot)
{
	struct ultrawidelock_dfu_hdr hdr;

	if (!s_rx.active || s_rx.got != s_rx.total) {
		return ULTRAWIDELOCK_DFU_ERR_SEQUENCE;
	}
	if (wbuf_flush(true) != 0) {
		return ULTRAWIDELOCK_DFU_ERR_FLASH;
	}

	memcpy(&hdr, s_rx.head, sizeof(hdr));

	if (hdr.magic != ULTRAWIDELOCK_DFU_MAGIC || hdr.abi_version != ULTRAWIDELOCK_DFU_ABI_VERSION ||
	    hdr.hdr_crc32 != ultrawidelock_crc32(s_rx.head, ULTRAWIDELOCK_DFU_HDR_CRC_LEN) ||
	    hdr.patch_len != (s_rx.total - HEAD_LEN) || hdr.patch_crc32 != s_rx.patch_crc) {
		LOG_WRN("rejected: staged bytes do not match the header");
		return ULTRAWIDELOCK_DFU_ERR_INTEGRITY;
	}

	/* The header goes in LAST. Until this write lands there is no magic in
	 * the staging partition and the bootloader has nothing to act on, so an
	 * interrupted transfer is indistinguishable from no transfer. */
	if (ultrawidelock_flash_write(s_fa, ULTRAWIDELOCK_DFU_HDR_OFFSET, s_rx.head,
			    ULTRAWIDELOCK_DFU_HDR_LEN) != 0) {
		return ULTRAWIDELOCK_DFU_ERR_FLASH;
	}

	s_rx.active = false;
	if (reboot) {
		(void)ultrawidelock_dwork_schedule(&s_reboot, 500);
	}
	return ULTRAWIDELOCK_DFU_ERR_OK;
}

static size_t do_begin(const uint8_t *frame, size_t len, uint8_t *rsp)
{
	enum ultrawidelock_dfu_err e;

	if (len < 5u) {
		return reply_err(rsp, ULTRAWIDELOCK_DFU_ERR_MALFORMED);
	}
	e = begin_at(sys_get_le32(&frame[1]));
	return (e == ULTRAWIDELOCK_DFU_ERR_OK) ? reply_ok(rsp) : reply_err(rsp, e);
}

static size_t do_data(const uint8_t *frame, size_t len, uint8_t *rsp)
{
	enum ultrawidelock_dfu_err e = feed_bytes(&frame[1], len - 1u);

	return (e == ULTRAWIDELOCK_DFU_ERR_OK) ? reply_ok(rsp) : reply_err(rsp, e);
}

static size_t do_commit(uint8_t *rsp)
{
	enum ultrawidelock_dfu_err e = commit_now(true);

	return (e == ULTRAWIDELOCK_DFU_ERR_OK) ? reply_ok(rsp) : reply_err(rsp, e);
}

int ultrawidelock_dfu_rx_frame(const uint8_t *frame, size_t len, uint8_t *rsp, size_t *rsp_len)
{
	if (len < 1u) {
		*rsp_len = reply_err(rsp, ULTRAWIDELOCK_DFU_ERR_MALFORMED);
		return 0;
	}
	if (!s_open) {
		*rsp_len = reply_err(rsp, ULTRAWIDELOCK_DFU_ERR_CLOSED);
		return 0;
	}

	switch (frame[0]) {
	case ULTRAWIDELOCK_DFU_OP_BEGIN:
		*rsp_len = do_begin(frame, len, rsp);
		break;
	case ULTRAWIDELOCK_DFU_OP_DATA:
		*rsp_len = do_data(frame, len, rsp);
		break;
	case ULTRAWIDELOCK_DFU_OP_COMMIT:
		*rsp_len = do_commit(rsp);
		break;
	case ULTRAWIDELOCK_DFU_OP_ABORT:
		if (s_fa != NULL) {
			(void)ultrawidelock_flash_erase(s_fa, 0, ultrawidelock_flash_size(s_fa));
		}
		ultrawidelock_dfu_rx_reset();
		*rsp_len = reply_ok(rsp);
		break;
	default:
		*rsp_len = reply_err(rsp, ULTRAWIDELOCK_DFU_ERR_MALFORMED);
		break;
	}

	return 0;
}

#ifdef CONFIG_ULTRAWIDELOCK_DFU_SMP_IMG

/* MCUboot's image magic and the two header fields needed to measure a wrapper.
 * Also spelled out in src/dfu_smp_img.c, which walks the running image's TLVs;
 * this is MCUboot's fixed on-disk format and cannot drift. */
#define MCUBOOT_IMAGE_MAGIC 0x96f3b83du
#define MCUBOOT_HDR_MIN     16u

/* The CLIENT's cursor, which stops matching the patch cursor the moment a file
 * is wrapped. Everything reported back to the host is in this space; s_rx.got
 * remains the cursor into the patch itself. */
static uint32_t s_cli_off;
static uint32_t s_skip;  /**< leading wrapper bytes to discard */
static uint32_t s_inner; /**< patch bytes inside the file */

int ultrawidelock_dfu_rx_upload(uint32_t off, uint32_t total, const uint8_t *data, size_t len,
				uint32_t *next)
{
	enum ultrawidelock_dfu_err e;

	if (!s_open) {
		return -EACCES;
	}

	if (off == 0U) {
		/* A restarted upload is normal, not an error: mcumgr clients
		 * rewind to 0 after a dropped link. Erasing and starting over
		 * is exactly what BEGIN does. */
		s_skip = 0U;
		s_inner = total;

		/* A phone will not offer a file its own parser rejects, so a
		 * patch meant for one is dressed as an MCUboot image by
		 * `ultrawidelock_patch.py wrap`. Step over that wrapper. A raw .wdfu has
		 * no magic here and is taken as it is, so both files work and
		 * the native transport is untouched. */
		if (len >= MCUBOOT_HDR_MIN && sys_get_le32(data) == MCUBOOT_IMAGE_MAGIC) {
			uint32_t hdr_sz = sys_get_le16(&data[8]);
			uint32_t img_sz = sys_get_le32(&data[12]);

			if (hdr_sz < MCUBOOT_HDR_MIN || img_sz == 0U || hdr_sz > total ||
			    img_sz > total - hdr_sz) {
				e = ULTRAWIDELOCK_DFU_ERR_MALFORMED;
				goto refused;
			}
			s_skip = hdr_sz;
			s_inner = img_sz;
			LOG_INF("mcuboot wrapper: %u B header, %u B payload", (unsigned)hdr_sz,
				(unsigned)img_sz);
		}

		e = begin_at(s_inner);
		if (e != ULTRAWIDELOCK_DFU_ERR_OK) {
			goto refused;
		}
		s_cli_off = 0U;
	} else if (off != s_cli_off) {
		/* Do NOT fail. mcumgr's contract is that the device reports
		 * where it actually is and the host resends from there, which
		 * is what makes a dropped chunk recoverable without restarting
		 * the whole transfer. */
		*next = s_cli_off;
		return 0;
	}

	/* The wrapper's header. */
	if (s_cli_off < s_skip) {
		size_t drop = MIN(len, (size_t)(s_skip - s_cli_off));

		data += drop;
		len -= drop;
		s_cli_off += drop;
	}

	/* The patch. */
	if (len > 0U && s_rx.got < s_inner) {
		size_t take = MIN(len, (size_t)(s_inner - s_rx.got));

		e = feed_bytes(data, take);
		if (e != ULTRAWIDELOCK_DFU_ERR_OK) {
			goto refused;
		}
		len -= take;
		s_cli_off += take;
	}

	/* The wrapper's TLV trailer. Acknowledged and thrown away: the client
	 * has to be allowed to finish sending the file it holds, and nothing
	 * past the patch means anything to the bootloader. */
	s_cli_off += len;

	/* The last patch byte stages the header but does not restart the board.
	 * The host sends os_mgmt reset itself, and a board that rebooted early
	 * would drop the response and read as a failed upload. */
	if (s_rx.active && s_rx.got == s_inner) {
		e = commit_now(false);
		if (e != ULTRAWIDELOCK_DFU_ERR_OK) {
			goto refused;
		}
		LOG_INF("update staged, %u B; awaiting reset", (unsigned)s_inner);
	}

	*next = s_cli_off;
	return 0;

refused:
	s_cli_off = 0U;
	LOG_WRN("upload refused at %u B (err %d)", (unsigned)s_rx.got, (int)e);
	ultrawidelock_dfu_rx_reset();
	return -EINVAL;
}

/**
 * Return true if a valid patch header is present in the staging flash area; otherwise return false.
 * The header's magic and ABI version must both match.
 */
bool ultrawidelock_dfu_rx_staged(void)
{
	struct ultrawidelock_dfu_hdr hdr;

	if (staging_open() != 0) {
		return false;
	}
	if (ultrawidelock_flash_read(s_fa, ULTRAWIDELOCK_DFU_HDR_OFFSET, &hdr, sizeof(hdr)) != 0) {
		return false;
	}
	return hdr.magic == ULTRAWIDELOCK_DFU_MAGIC && hdr.abi_version == ULTRAWIDELOCK_DFU_ABI_VERSION;
}

#endif /* CONFIG_ULTRAWIDELOCK_DFU_SMP_IMG */
