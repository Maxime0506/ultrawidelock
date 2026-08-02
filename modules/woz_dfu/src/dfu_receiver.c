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
 *     0   32   struct woz_dfu_hdr
 *    32   64   ECDSA-P256 signature, raw r||s, over those 32 bytes
 *    96   ..   the patch
 *
 * The header is written to flash LAST, after the whole patch has arrived and
 * its CRC has been checked. So a transfer that is cut off leaves a staging
 * partition with no valid magic in it, and the next boot ignores it. There is
 * no half-staged state that the bootloader can act on.
 *
 * THE SIGNATURE IS CHECKED HERE, NOT IN THE BOOTLOADER. This image already has
 * PSA ECDSA-P256 linked for Aliro; MCUboot is the flash-starved one. And the
 * floor sits under both: CONFIG_BOOT_VALIDATE_SLOT0 makes MCUboot re-verify
 * the P-256 signature of the RESULT before booting it, so even a forged header
 * cannot install code -- only destroy the installed image, which recovery
 * catches.
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <psa/crypto.h>
#include <string.h>

#include <pm_config.h>

#include "woz_dfu.h"
#include "woz_dfu_rx.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(woz_dfu, CONFIG_WOZ_DFU_LOG_LEVEL);

/** Generated at build time from the MCUboot signing key. 0x04 || X || Y. */
extern const uint8_t woz_dfu_pubkey[65];

/** Bytes of preamble ahead of the patch: the header and its signature. */
#define HEAD_LEN (WOZ_DFU_HDR_LEN + WOZ_DFU_SIG_LEN)

/** Room for patch bytes, after the header page and the step-log page. */
#define PATCH_MAX (PM_PATCH_STAGING_SIZE - WOZ_DFU_PATCH_OFFSET)

/** Flash write staging. Multiple of the 4-byte write block. */
#define WBUF_SZ 64

static struct {
	bool active;
	uint32_t total; /**< wire bytes the host promised */
	uint32_t got;   /**< wire bytes consumed so far */
	uint32_t patch_crc;
	off_t wpos; /**< next staging offset for patch data */
	uint8_t head[HEAD_LEN];
	uint8_t wbuf[WBUF_SZ];
	size_t wlen;
} s_rx;

static const struct flash_area *s_fa;

/* Replying before rebooting is the whole reason this is deferred: a reboot
 * inside the frame handler drops the acknowledgement the host is waiting for,
 * and the host cannot then tell success from a dead board. */
static void reboot_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("update staged, restarting into the bootloader");
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(s_reboot, reboot_fn);

/* ---- window --------------------------------------------------------------- */

static void window_expire(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_window, window_expire);
static bool s_open;

static void window_expire(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("update window closed");
	s_open = false;
	woz_dfu_rx_reset();
}

void woz_dfu_window_open(uint32_t duration_ms)
{
	s_open = true;
	(void)k_work_reschedule(&s_window, K_MSEC(duration_ms));
	LOG_INF("update window open for %u ms", (unsigned)duration_ms);
}

void woz_dfu_window_close(void)
{
	(void)k_work_cancel_delayable(&s_window);
	s_open = false;
	woz_dfu_rx_reset();
}

bool woz_dfu_window_is_open(void)
{
	return s_open;
}

/* ---- staging flash -------------------------------------------------------- */

static int staging_open(void)
{
	if (s_fa != NULL) {
		return 0;
	}
	return flash_area_open(PM_PATCH_STAGING_ID, &s_fa);
}

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
	if (flash_area_write(s_fa, s_rx.wpos, s_rx.wbuf, n) != 0) {
		return -1;
	}

	s_rx.wpos += (off_t)n;
	s_rx.wlen -= n;
	if (s_rx.wlen > 0U) {
		memmove(s_rx.wbuf, s_rx.wbuf + n, s_rx.wlen);
	}
	return 0;
}

static int patch_write(const uint8_t *data, size_t len)
{
	s_rx.patch_crc = crc32_ieee_update(s_rx.patch_crc, data, len);

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

static bool head_verifies(void)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t st;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);

	st = psa_import_key(&attr, woz_dfu_pubkey, sizeof(woz_dfu_pubkey), &key);
	if (st != PSA_SUCCESS) {
		LOG_ERR("pubkey import failed (%d)", (int)st);
		return false;
	}

	st = psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), s_rx.head, WOZ_DFU_HDR_LEN,
				s_rx.head + WOZ_DFU_HDR_LEN, WOZ_DFU_SIG_LEN);
	(void)psa_destroy_key(key);

	return st == PSA_SUCCESS;
}

/* ---- frame handling ------------------------------------------------------- */

void woz_dfu_rx_reset(void)
{
	memset(&s_rx, 0, sizeof(s_rx));
}

static size_t reply_ok(uint8_t *rsp)
{
	rsp[0] = WOZ_DFU_RSP_OK;
	sys_put_le32(s_rx.got, &rsp[1]);
	return 5u;
}

static size_t reply_err(uint8_t *rsp, enum woz_dfu_err code)
{
	woz_dfu_rx_reset();
	rsp[0] = WOZ_DFU_RSP_ERR;
	rsp[1] = (uint8_t)code;
	return 2u;
}

static size_t do_begin(const uint8_t *frame, size_t len, uint8_t *rsp)
{
	uint32_t total;

	if (len < 5u) {
		return reply_err(rsp, WOZ_DFU_ERR_MALFORMED);
	}
	total = sys_get_le32(&frame[1]);
	if (total <= HEAD_LEN || (total - HEAD_LEN) > PATCH_MAX) {
		return reply_err(rsp, WOZ_DFU_ERR_SIZE);
	}
	if (staging_open() != 0) {
		return reply_err(rsp, WOZ_DFU_ERR_FLASH);
	}

	/* Erase everything, including the step log: a stale one would make the
	 * bootloader believe steps of THIS patch were already applied. */
	if (flash_area_erase(s_fa, 0, PM_PATCH_STAGING_SIZE) != 0) {
		return reply_err(rsp, WOZ_DFU_ERR_FLASH);
	}

	woz_dfu_rx_reset();
	s_rx.active = true;
	s_rx.total = total;
	s_rx.wpos = (off_t)WOZ_DFU_PATCH_OFFSET;
	LOG_INF("update begun, %u B", (unsigned)total);
	return reply_ok(rsp);
}

static size_t do_data(const uint8_t *frame, size_t len, uint8_t *rsp)
{
	const uint8_t *p = &frame[1];
	size_t n = len - 1u;

	if (!s_rx.active) {
		return reply_err(rsp, WOZ_DFU_ERR_SEQUENCE);
	}
	if (s_rx.got + n > s_rx.total) {
		return reply_err(rsp, WOZ_DFU_ERR_SIZE);
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
			return reply_err(rsp, WOZ_DFU_ERR_AUTH);
		}
	}

	if (n > 0U) {
		if (patch_write(p, n) != 0) {
			return reply_err(rsp, WOZ_DFU_ERR_FLASH);
		}
		s_rx.got += n;
	}

	return reply_ok(rsp);
}

static size_t do_commit(uint8_t *rsp)
{
	struct woz_dfu_hdr hdr;

	if (!s_rx.active || s_rx.got != s_rx.total) {
		return reply_err(rsp, WOZ_DFU_ERR_SEQUENCE);
	}
	if (wbuf_flush(true) != 0) {
		return reply_err(rsp, WOZ_DFU_ERR_FLASH);
	}

	memcpy(&hdr, s_rx.head, sizeof(hdr));

	if (hdr.magic != WOZ_DFU_MAGIC || hdr.abi_version != WOZ_DFU_ABI_VERSION ||
	    hdr.hdr_crc32 != crc32_ieee(s_rx.head, WOZ_DFU_HDR_CRC_LEN) ||
	    hdr.patch_len != (s_rx.total - HEAD_LEN) || hdr.patch_crc32 != s_rx.patch_crc) {
		LOG_WRN("rejected: staged bytes do not match the header");
		return reply_err(rsp, WOZ_DFU_ERR_INTEGRITY);
	}

	/* The header goes in LAST. Until this write lands there is no magic in
	 * the staging partition and the bootloader has nothing to act on, so an
	 * interrupted transfer is indistinguishable from no transfer. */
	if (flash_area_write(s_fa, (off_t)WOZ_DFU_HDR_OFFSET, s_rx.head, WOZ_DFU_HDR_LEN) != 0) {
		return reply_err(rsp, WOZ_DFU_ERR_FLASH);
	}

	s_rx.active = false;
	(void)k_work_schedule(&s_reboot, K_MSEC(500));
	return reply_ok(rsp);
}

int woz_dfu_rx_frame(const uint8_t *frame, size_t len, uint8_t *rsp, size_t *rsp_len)
{
	if (len < 1u) {
		*rsp_len = reply_err(rsp, WOZ_DFU_ERR_MALFORMED);
		return 0;
	}
	if (!s_open) {
		*rsp_len = reply_err(rsp, WOZ_DFU_ERR_CLOSED);
		return 0;
	}

	switch (frame[0]) {
	case WOZ_DFU_OP_BEGIN:
		*rsp_len = do_begin(frame, len, rsp);
		break;
	case WOZ_DFU_OP_DATA:
		*rsp_len = do_data(frame, len, rsp);
		break;
	case WOZ_DFU_OP_COMMIT:
		*rsp_len = do_commit(rsp);
		break;
	case WOZ_DFU_OP_ABORT:
		if (s_fa != NULL) {
			(void)flash_area_erase(s_fa, 0, PM_PATCH_STAGING_SIZE);
		}
		woz_dfu_rx_reset();
		*rsp_len = reply_ok(rsp);
		break;
	default:
		*rsp_len = reply_err(rsp, WOZ_DFU_ERR_MALFORMED);
		break;
	}

	return 0;
}
