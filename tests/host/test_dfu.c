/**
 * @file test_dfu.c — the delta-update halves on host, over tests/host/dfufake.
 *
 * Files under test:
 *   modules/woz_dfu/src/dfu_receiver.c  application side: frame, verify, stage
 *   modules/woz_dfu/src/dfu_applier.c   bootloader side: apply, resume, consume
 *
 * WHAT THESE CHECKS ARE WORTH. The flash is honest: RAM-backed partitions that
 * reject a write which is not word-offset and word-sized and an erase which is
 * not page-offset and page-sized, exactly as the nRF driver does. That makes
 * the applier's write combiner and its ROUND_UP on erase load-bearing here —
 * remove either and these tests fail. CRC-32 is the real IEEE polynomial, so
 * the integrity gates are checked against the same number scripts/woz_patch.py
 * computes.
 *
 * WHAT THEY ARE NOT WORTH. psafake does no crypto: every signature check
 * passes or fails because a knob says so, so nothing here shows that a forged
 * patch is rejected on the board — only that the AUTH branch is taken when the
 * verify call reports failure. detools is a scripted double, not the vendored
 * patcher: these tests drive the applier's five callbacks with chosen memory
 * operations and never validate a patch format.
 *
 * ORDER MATTERS IN ONE PLACE, and it is called out where it does: dfu_receiver
 * caches its flash area in a file-static that nothing resets, so the
 * open-failure case has to run before any successful open.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dfufake.h"
#include "psafake.h"
#include "test.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "woz_dfu.h"
#include "woz_dfu_rx.h"

/* dfu_receiver.c links this from a generated file on target. */
const uint8_t woz_dfu_pubkey[65] = {0x04};

/* dfu_applier.c's SYS_INIT, made callable by logfake's <zephyr/init.h>. */
extern int (*const logfake_sys_init_woz_dfu_apply)(void);

#define HEAD_LEN  (WOZ_DFU_HDR_LEN + WOZ_DFU_SIG_LEN)
#define PATCH_MAX (DFUFAKE_STAGING_SIZE - WOZ_DFU_PATCH_OFFSET)

/* ---- helpers -------------------------------------------------------------- */

/**
 * Build the 96-byte preamble: a self-consistent header followed by a signature
 * the fake never inspects. Every field the two halves cross-check is filled in,
 * so a suite that wants a rejection has to break one on purpose.
 */
static void build_head(uint8_t *head, const uint8_t *patch, uint32_t patch_len, uint32_t from_len,
		       uint32_t from_crc)
{
	struct woz_dfu_hdr hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = WOZ_DFU_MAGIC;
	hdr.abi_version = WOZ_DFU_ABI_VERSION;
	hdr.flags = 0;
	hdr.patch_len = patch_len;
	hdr.to_len = from_len;
	hdr.patch_crc32 = crc32_ieee(patch, patch_len);
	hdr.from_crc32 = from_crc;
	hdr.from_len = from_len;
	hdr.hdr_crc32 = 0;
	memcpy(head, &hdr, sizeof(hdr));
	hdr.hdr_crc32 = crc32_ieee(head, WOZ_DFU_HDR_CRC_LEN);
	memcpy(head, &hdr, sizeof(hdr));
	memset(head + WOZ_DFU_HDR_LEN, 0xa5, WOZ_DFU_SIG_LEN);
}

/** Deterministic filler so a staged patch can be compared byte for byte. */
static void fill_pattern(uint8_t *dst, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		dst[i] = (uint8_t)(seed + i * 7u);
	}
}

/** One framed request through the receiver; returns the reply length. */
static size_t frame(const uint8_t *req, size_t len, uint8_t *rsp)
{
	size_t rsp_len = 0;

	T_EQ("rx_frame returns 0", woz_dfu_rx_frame(req, len, rsp, &rsp_len), 0);
	return rsp_len;
}

/** BEGIN with an explicit wire total. */
static size_t send_begin(uint32_t total, uint8_t *rsp)
{
	uint8_t req[5] = {WOZ_DFU_OP_BEGIN};

	req[1] = (uint8_t)total;
	req[2] = (uint8_t)(total >> 8);
	req[3] = (uint8_t)(total >> 16);
	req[4] = (uint8_t)(total >> 24);
	return frame(req, sizeof(req), rsp);
}

/** DATA carrying @p len payload bytes. */
static size_t send_data(const uint8_t *payload, size_t len, uint8_t *rsp)
{
	uint8_t req[512];

	req[0] = WOZ_DFU_OP_DATA;
	memcpy(req + 1, payload, len);
	return frame(req, len + 1u, rsp);
}

/** True when the reply is OK and reports @p got bytes received. */
static int is_ok(const uint8_t *rsp, size_t rsp_len, uint32_t got)
{
	uint32_t reported;

	if (rsp_len != 5u || rsp[0] != WOZ_DFU_RSP_OK) {
		return 0;
	}
	reported = (uint32_t)rsp[1] | ((uint32_t)rsp[2] << 8) | ((uint32_t)rsp[3] << 16) |
		   ((uint32_t)rsp[4] << 24);
	return reported == got;
}

/** True when the reply is ERR carrying @p code. */
static int is_err(const uint8_t *rsp, size_t rsp_len, enum woz_dfu_err code)
{
	return rsp_len == 2u && rsp[0] == WOZ_DFU_RSP_ERR && rsp[1] == (uint8_t)code;
}

/** Put the receiver in a known state: window open, nothing in flight. */
static void receiver_ready(void)
{
	dfufake_reset();
	psafake_reset();
	woz_dfu_rx_reset();
	woz_dfu_window_open(1000);
}

/* ---- receiver: the update window ------------------------------------------ */

static void window_events(bool open);
static int window_open_seen;
static int window_close_seen;

static void window_events(bool open)
{
	if (open) {
		window_open_seen++;
	} else {
		window_close_seen++;
	}
}

static void test_window(void)
{
	uint8_t rsp[WOZ_DFU_RSP_MAX];

	t_group("dfu window");

	dfufake_reset();
	woz_dfu_window_close();
	T_OK("closed at rest", !woz_dfu_window_is_open());

	/* Every frame is refused while shut, and the refusal is the one code a
	 * legitimate owner can act on. */
	T_OK("frame refused while closed", is_err(rsp, send_begin(200, rsp), WOZ_DFU_ERR_CLOSED));

	window_open_seen = 0;
	window_close_seen = 0;
	woz_dfu_set_window_cb(window_events);

	woz_dfu_window_open(1234);
	T_OK("open after open", woz_dfu_window_is_open());
	T_EQ("open notified once", window_open_seen, 1);
	T_EQ("expiry rescheduled", (long)workfake.reschedule_calls, 1L);
	T_EQ("expiry delay is the argument", (long)workfake.last_delay, 1234L);

	/* Re-opening restarts the clock rather than stacking timers. */
	woz_dfu_window_open(500);
	T_EQ("reopen reschedules", (long)workfake.reschedule_calls, 2L);
	T_EQ("reopen delay", (long)workfake.last_delay, 500L);
	T_EQ("reopen notifies again", window_open_seen, 2);

	/* Fire the expiry work the way the kernel would. */
	workfake.last->work.handler(&workfake.last->work);
	T_OK("closed after expiry", !woz_dfu_window_is_open());
	T_EQ("close notified", window_close_seen, 1);

	woz_dfu_window_open(500);
	{
		const unsigned before = workfake.cancel_calls;

		woz_dfu_window_close();
		T_OK("closed by request", !woz_dfu_window_is_open());
		T_EQ("close cancels the timer", (long)(workfake.cancel_calls - before), 1L);
	}
	T_EQ("close notified again", window_close_seen, 2);

	/* A port with no indicator pays nothing: the callback is optional. */
	woz_dfu_set_window_cb(NULL);
	woz_dfu_window_open(10);
	woz_dfu_window_close();
	T_EQ("no callback, no extra notifications", window_close_seen, 2);
}

/* ---- receiver: framing and refusals --------------------------------------- */

static void test_receiver_framing(void)
{
	uint8_t rsp[WOZ_DFU_RSP_MAX];
	uint8_t head[HEAD_LEN];
	uint8_t patch[64];

	t_group("dfu receiver framing");

	/* MUST BE FIRST: staging_open() caches the flash area in a file-static
	 * that nothing resets, so once an open has succeeded this branch is
	 * unreachable for the rest of the process. */
	dfufake_reset();
	psafake_reset();
	woz_dfu_rx_reset();
	woz_dfu_window_open(1000);
	dfufake_staging.fail_open = true;
	T_OK("begin refused when staging will not open",
	     is_err(rsp, send_begin(200, rsp), WOZ_DFU_ERR_FLASH));
	dfufake_staging.fail_open = false;

	receiver_ready();
	T_OK("empty frame is malformed", is_err(rsp, frame(rsp, 0, rsp), WOZ_DFU_ERR_MALFORMED));

	receiver_ready();
	{
		const uint8_t unknown[] = {0x7f};

		T_OK("unknown opcode is malformed",
		     is_err(rsp, frame(unknown, sizeof(unknown), rsp), WOZ_DFU_ERR_MALFORMED));
	}

	receiver_ready();
	{
		const uint8_t truncated[] = {WOZ_DFU_OP_BEGIN, 0x01, 0x02};

		T_OK("short begin is malformed",
		     is_err(rsp, frame(truncated, sizeof(truncated), rsp), WOZ_DFU_ERR_MALFORMED));
	}

	/* Size gates: a total that cannot carry a preamble, and one whose patch
	 * would not fit the partition. */
	receiver_ready();
	T_OK("total at the preamble length is too small",
	     is_err(rsp, send_begin(HEAD_LEN, rsp), WOZ_DFU_ERR_SIZE));
	receiver_ready();
	T_OK("total past the staging room is too large",
	     is_err(rsp, send_begin(HEAD_LEN + PATCH_MAX + 1u, rsp), WOZ_DFU_ERR_SIZE));
	receiver_ready();
	T_OK("total at exactly the staging room is accepted",
	     is_ok(rsp, send_begin(HEAD_LEN + PATCH_MAX, rsp), 0));

	/* DATA before BEGIN has nowhere to go. */
	receiver_ready();
	fill_pattern(patch, sizeof(patch), 1);
	T_OK("data before begin is out of sequence",
	     is_err(rsp, send_data(patch, sizeof(patch), rsp), WOZ_DFU_ERR_SEQUENCE));

	/* More bytes than promised. */
	receiver_ready();
	T_OK("begin accepted", is_ok(rsp, send_begin(HEAD_LEN + 8u, rsp), 0));
	build_head(head, patch, 8, 0, 0);
	T_OK("preamble accepted", is_ok(rsp, send_data(head, HEAD_LEN, rsp), HEAD_LEN));
	T_OK("overrun refused", is_err(rsp, send_data(patch, 9, rsp), WOZ_DFU_ERR_SIZE));

	/* An erase failure at BEGIN is reported as flash, not as size. */
	receiver_ready();
	dfufake_staging.erase_fail_in = 0;
	T_OK("begin refused when the erase fails",
	     is_err(rsp, send_begin(HEAD_LEN + 8u, rsp), WOZ_DFU_ERR_FLASH));

	/* COMMIT before the promised bytes have arrived. */
	receiver_ready();
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + 8u, rsp), 0));
	{
		const uint8_t commit[] = {WOZ_DFU_OP_COMMIT};

		T_OK("early commit is out of sequence",
		     is_err(rsp, frame(commit, sizeof(commit), rsp), WOZ_DFU_ERR_SEQUENCE));
	}
}

/* ---- receiver: authenticity ----------------------------------------------- */

static void test_receiver_auth(void)
{
	uint8_t rsp[WOZ_DFU_RSP_MAX];
	uint8_t head[HEAD_LEN];
	uint8_t patch[16];

	t_group("dfu receiver authenticity");

	fill_pattern(patch, sizeof(patch), 9);
	build_head(head, patch, sizeof(patch), 0, 0);

	/* The signature is checked the moment the preamble is complete, not at
	 * commit — a forged header never reaches the flash. */
	receiver_ready();
	psafake.verify_ret = -1;
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + sizeof(patch), rsp), 0));
	T_OK("bad signature refused", is_err(rsp, send_data(head, HEAD_LEN, rsp), WOZ_DFU_ERR_AUTH));
	T_EQ("verified over the header only", (long)psafake.last_msg_len, (long)WOZ_DFU_HDR_LEN);
	T_EQ("signature length", (long)psafake.last_sig_len, (long)WOZ_DFU_SIG_LEN);
	T_EQ("key destroyed after the verify", (long)psafake.destroy_calls, 1L);

	/* A key that will not import fails closed. */
	receiver_ready();
	psafake.import_ret = -1;
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + sizeof(patch), rsp), 0));
	T_OK("import failure refused",
	     is_err(rsp, send_data(head, HEAD_LEN, rsp), WOZ_DFU_ERR_AUTH));

	/* The preamble may arrive split across frames; the check fires once, on
	 * the frame that completes it. */
	receiver_ready();
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + sizeof(patch), rsp), 0));
	T_OK("first half buffered", is_ok(rsp, send_data(head, 40, rsp), 40));
	T_EQ("no verify yet", (long)psafake.verify_calls, 0L);
	T_OK("second half completes the preamble",
	     is_ok(rsp, send_data(head + 40, HEAD_LEN - 40, rsp), HEAD_LEN));
	T_EQ("verified once", (long)psafake.verify_calls, 1L);
}

/* ---- receiver: the happy path --------------------------------------------- */

static void test_receiver_stage(void)
{
	uint8_t rsp[WOZ_DFU_RSP_MAX];
	uint8_t head[HEAD_LEN];
	/* 70 is deliberately neither a multiple of the 4-byte write block nor of
	 * the 64-byte staging buffer: it exercises one full flush and one padded
	 * tail, which is the only way the pad-to-word path runs. */
	uint8_t patch[70];
	size_t i;

	t_group("dfu receiver staging");

	fill_pattern(patch, sizeof(patch), 3);
	build_head(head, patch, sizeof(patch), 0, 0);

	receiver_ready();
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + sizeof(patch), rsp), 0));
	T_EQ("staging erased whole at begin", (long)dfufake_staging.last_erase_len,
	     (long)DFUFAKE_STAGING_SIZE);
	T_EQ("erase started at zero", (long)dfufake_staging.last_erase_off, 0L);

	T_OK("preamble", is_ok(rsp, send_data(head, HEAD_LEN, rsp), HEAD_LEN));
	T_OK("patch", is_ok(rsp, send_data(patch, sizeof(patch), rsp), HEAD_LEN + sizeof(patch)));

	/* Nothing is in the header page yet: an interrupted transfer must be
	 * indistinguishable from no transfer. */
	T_EQ("no magic before commit", (long)dfufake_peek(&dfufake_staging, 0), 0xffL);

	{
		const uint8_t commit[] = {WOZ_DFU_OP_COMMIT};

		T_OK("commit", is_ok(rsp, frame(commit, sizeof(commit), rsp),
				     HEAD_LEN + sizeof(patch)));
	}

	/* The header landed, and it is the one that was verified. */
	T_EQ("magic byte 0", (long)dfufake_peek(&dfufake_staging, 0), (long)(WOZ_DFU_MAGIC & 0xff));
	T_OK("header matches the preamble",
	     memcmp(dfufake_staging.buf, head, WOZ_DFU_HDR_LEN) == 0);

	/* The patch landed at the patch offset, byte for byte, and the pad is
	 * erased bits rather than stale data. */
	T_OK("patch bytes staged",
	     memcmp(dfufake_staging.buf + WOZ_DFU_PATCH_OFFSET, patch, sizeof(patch)) == 0);
	for (i = sizeof(patch); i < 72u; i++) {
		T_EQ("tail padded with erased bits",
		     (long)dfufake_peek(&dfufake_staging, WOZ_DFU_PATCH_OFFSET + i), 0xffL);
	}

	/* The reply goes out first; only then does the board restart. */
	T_EQ("reboot deferred, not immediate", (long)dfufake.reboot_calls, 0L);
	T_EQ("reboot scheduled", (long)workfake.schedule_calls, 1L);
	workfake.last->work.handler(&workfake.last->work);
	T_EQ("reboot taken", (long)dfufake.reboot_calls, 1L);
	T_EQ("cold reboot", (long)dfufake.last_reboot_type, (long)SYS_REBOOT_COLD);
}

/* ---- receiver: commit-time integrity and flash failures -------------------- */

/** Stage a complete transfer whose preamble the caller may have corrupted. */
static void stage_with_head(uint8_t *head, const uint8_t *patch, size_t patch_len, uint8_t *rsp)
{
	receiver_ready();
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + patch_len, rsp), 0));
	T_OK("preamble", is_ok(rsp, send_data(head, HEAD_LEN, rsp), HEAD_LEN));
	T_OK("patch", is_ok(rsp, send_data(patch, patch_len, rsp), HEAD_LEN + patch_len));
}

static size_t send_commit(uint8_t *rsp)
{
	const uint8_t commit[] = {WOZ_DFU_OP_COMMIT};

	return frame(commit, sizeof(commit), rsp);
}

static void test_receiver_integrity(void)
{
	uint8_t rsp[WOZ_DFU_RSP_MAX];
	uint8_t head[HEAD_LEN];
	uint8_t patch[32];

	t_group("dfu receiver integrity");

	fill_pattern(patch, sizeof(patch), 5);

	/* Each field the commit cross-checks, broken one at a time. The header
	 * CRC is recomputed only where the point is a different field. */
	build_head(head, patch, sizeof(patch), 0, 0);
	head[0] ^= 0xffu; /* magic */
	stage_with_head(head, patch, sizeof(patch), rsp);
	T_OK("wrong magic refused", is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_INTEGRITY));

	build_head(head, patch, sizeof(patch), 0, 0);
	head[4] = 0x7f; /* abi_version */
	stage_with_head(head, patch, sizeof(patch), rsp);
	T_OK("wrong abi refused", is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_INTEGRITY));

	build_head(head, patch, sizeof(patch), 0, 0);
	head[WOZ_DFU_HDR_CRC_LEN] ^= 0x01u; /* hdr_crc32 itself */
	stage_with_head(head, patch, sizeof(patch), rsp);
	T_OK("wrong header crc refused", is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_INTEGRITY));

	/* patch_len that disagrees with what actually arrived. */
	build_head(head, patch, sizeof(patch) - 1u, 0, 0);
	stage_with_head(head, patch, sizeof(patch), rsp);
	T_OK("wrong patch length refused", is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_INTEGRITY));

	/* Right length, wrong bytes: the running CRC catches it. */
	build_head(head, patch, sizeof(patch), 0, 0);
	{
		uint8_t corrupted[32];

		memcpy(corrupted, patch, sizeof(corrupted));
		corrupted[7] ^= 0x80u;
		stage_with_head(head, corrupted, sizeof(corrupted), rsp);
		T_OK("wrong patch crc refused",
		     is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_INTEGRITY));
	}

	/* A flash failure while writing the header is reported as flash. */
	build_head(head, patch, sizeof(patch), 0, 0);
	stage_with_head(head, patch, sizeof(patch), rsp);
	dfufake_staging.write_fail_in = 0;
	T_OK("header write failure refused", is_err(rsp, send_commit(rsp), WOZ_DFU_ERR_FLASH));

	/* A flash failure mid-patch is reported as flash, and only once: the
	 * error resets the transfer. */
	receiver_ready();
	build_head(head, patch, sizeof(patch), 0, 0);
	T_OK("begin", is_ok(rsp, send_begin(HEAD_LEN + 200u, rsp), 0));
	T_OK("preamble", is_ok(rsp, send_data(head, HEAD_LEN, rsp), HEAD_LEN));
	dfufake_staging.write_fail_in = 0;
	{
		uint8_t big[200];

		fill_pattern(big, sizeof(big), 11);
		T_OK("patch write failure refused",
		     is_err(rsp, send_data(big, sizeof(big), rsp), WOZ_DFU_ERR_FLASH));
	}

	/* ABORT erases what was staged and answers OK. */
	build_head(head, patch, sizeof(patch), 0, 0);
	stage_with_head(head, patch, sizeof(patch), rsp);
	{
		const uint8_t abort_req[] = {WOZ_DFU_OP_ABORT};
		unsigned before = dfufake_staging.erase_calls;

		T_OK("abort", is_ok(rsp, frame(abort_req, sizeof(abort_req), rsp), 0));
		T_EQ("abort erased staging", (long)(dfufake_staging.erase_calls - before), 1L);
		T_EQ("abort erased the whole partition", (long)dfufake_staging.last_erase_len,
		     (long)DFUFAKE_STAGING_SIZE);
	}
}

/* ---- receiver: the SMP upload front door ---------------------------------- */

/** Build an MCUboot-shaped wrapper around @p patch, as `woz_patch.py wrap` does. */
static size_t wrap_mcuboot(uint8_t *out, const uint8_t *patch, size_t patch_len, uint16_t hdr_sz,
			   uint32_t img_sz, size_t trailer)
{
	memset(out, 0, hdr_sz);
	out[0] = 0x3d;
	out[1] = 0xb8;
	out[2] = 0xf3;
	out[3] = 0x96; /* 0x96f3b83d little-endian */
	out[8] = (uint8_t)hdr_sz;
	out[9] = (uint8_t)(hdr_sz >> 8);
	out[12] = (uint8_t)img_sz;
	out[13] = (uint8_t)(img_sz >> 8);
	out[14] = (uint8_t)(img_sz >> 16);
	out[15] = (uint8_t)(img_sz >> 24);
	memcpy(out + hdr_sz, patch, patch_len);
	memset(out + hdr_sz + patch_len, 0x5a, trailer);
	return hdr_sz + patch_len + trailer;
}

static void test_receiver_upload(void)
{
	uint8_t head[HEAD_LEN];
	uint8_t patch[48];
	uint8_t wire[512];
	uint8_t file[640];
	uint32_t next = 0;
	size_t total;

	t_group("dfu receiver smp upload");

	fill_pattern(patch, sizeof(patch), 7);
	build_head(head, patch, sizeof(patch), 0, 0);
	memcpy(wire, head, HEAD_LEN);
	memcpy(wire + HEAD_LEN, patch, sizeof(patch));
	total = HEAD_LEN + sizeof(patch);

	/* The window is the whole authorization model on this path too. */
	dfufake_reset();
	psafake_reset();
	woz_dfu_rx_reset();
	woz_dfu_window_close();
	T_EQ("upload refused while closed",
	     woz_dfu_rx_upload(0, (uint32_t)total, wire, total, &next), -EACCES);

	/* A raw .wdfu has no MCUboot magic and is taken as it is, in one chunk. */
	receiver_ready();
	T_EQ("raw upload accepted", woz_dfu_rx_upload(0, (uint32_t)total, wire, total, &next), 0);
	T_EQ("next offset is the whole file", (long)next, (long)total);
	T_OK("staged", woz_dfu_rx_staged());
	/* SMP stages and stops: the host sends its own reset, and a board that
	 * restarted here would drop the response. */
	T_EQ("no reboot scheduled on the SMP path", (long)workfake.schedule_calls, 0L);
	T_OK("header written", memcmp(dfufake_staging.buf, head, WOZ_DFU_HDR_LEN) == 0);

	/* Chunked, which is what a real client does. */
	receiver_ready();
	{
		size_t off = 0;
		const size_t chunk = 32;

		while (off < total) {
			size_t n = (total - off < chunk) ? total - off : chunk;

			T_EQ("chunk accepted",
			     woz_dfu_rx_upload((uint32_t)off, (uint32_t)total, wire + off, n,
					       &next),
			     0);
			off += n;
			T_EQ("device reports its position", (long)next, (long)off);
		}
		T_OK("staged after chunking", woz_dfu_rx_staged());
	}

	/* A mismatched offset is a resync, not a failure: the device says where
	 * it is and the host resends from there. */
	receiver_ready();
	T_EQ("first chunk", woz_dfu_rx_upload(0, (uint32_t)total, wire, 32, &next), 0);
	next = 0xdeadbeef;
	T_EQ("stale offset resyncs", woz_dfu_rx_upload(999, (uint32_t)total, wire, 32, &next), 0);
	T_EQ("resync reports the real position", (long)next, 32L);

	/* The MCUboot wrapper a phone will accept: header stepped over, trailer
	 * acknowledged and discarded. */
	receiver_ready();
	{
		size_t file_len = wrap_mcuboot(file, wire, total, 32, (uint32_t)total, 48);

		T_EQ("wrapped upload accepted",
		     woz_dfu_rx_upload(0, (uint32_t)file_len, file, file_len, &next), 0);
		T_EQ("next offset covers the whole file", (long)next, (long)file_len);
		T_OK("wrapper staged the inner patch", woz_dfu_rx_staged());
		T_OK("header is the inner one",
		     memcmp(dfufake_staging.buf, head, WOZ_DFU_HDR_LEN) == 0);
	}

	/* Wrapper fields that do not describe the file are refused rather than
	 * used to index outside it. */
	receiver_ready();
	{
		size_t file_len = wrap_mcuboot(file, wire, total, 32, (uint32_t)total, 48);

		file[8] = 8; /* hdr_sz below the 16-byte minimum */
		T_EQ("undersized wrapper header refused",
		     woz_dfu_rx_upload(0, (uint32_t)file_len, file, file_len, &next), -EINVAL);
	}
	receiver_ready();
	{
		size_t file_len = wrap_mcuboot(file, wire, total, 32, 0, 48);

		T_EQ("empty wrapper payload refused",
		     woz_dfu_rx_upload(0, (uint32_t)file_len, file, file_len, &next), -EINVAL);
	}
	receiver_ready();
	{
		size_t file_len = wrap_mcuboot(file, wire, total, 32, (uint32_t)total, 48);

		file[12] = 0xff;
		file[13] = 0xff;
		file[14] = 0xff;
		file[15] = 0x7f; /* img_sz past the end of the file */
		T_EQ("oversized wrapper payload refused",
		     woz_dfu_rx_upload(0, (uint32_t)file_len, file, file_len, &next), -EINVAL);
	}

	/* A size the partition cannot hold is refused at the first chunk. */
	receiver_ready();
	T_EQ("oversized upload refused",
	     woz_dfu_rx_upload(0, HEAD_LEN + PATCH_MAX + 1u, wire, 32, &next), -EINVAL);

	/* A signature failure inside the upload discards the transfer. */
	receiver_ready();
	psafake.verify_ret = -1;
	T_EQ("bad signature refused", woz_dfu_rx_upload(0, (uint32_t)total, wire, total, &next),
	     -EINVAL);
	T_OK("nothing staged after a refusal", !woz_dfu_rx_staged());

	/* Integrity failure at the implicit commit. */
	receiver_ready();
	{
		uint8_t bad[512];

		memcpy(bad, wire, total);
		bad[WOZ_DFU_HDR_CRC_LEN] ^= 0x01u;
		T_EQ("bad header crc refused",
		     woz_dfu_rx_upload(0, (uint32_t)total, bad, total, &next), -EINVAL);
	}

	/* rx_staged() reads flash, so a partition with no magic reports nothing
	 * staged even after a successful transfer elsewhere. */
	receiver_ready();
	T_OK("blank staging is not staged", !woz_dfu_rx_staged());
	dfufake_staging.read_fail_in = 0;
	T_OK("unreadable staging is not staged", !woz_dfu_rx_staged());
}

/* ---- applier -------------------------------------------------------------- */

/** Write a complete, self-consistent update into the staging partition. */
static void stage_for_apply(const uint8_t *patch, uint32_t patch_len, uint32_t from_len)
{
	uint8_t head[HEAD_LEN];
	uint32_t from_crc;

	dfufake_blank(&dfufake_staging);
	dfufake_blank(&dfufake_primary);
	fill_pattern(dfufake_primary.buf, from_len, 0x21);
	from_crc = crc32_ieee(dfufake_primary.buf, from_len);

	build_head(head, patch, patch_len, from_len, from_crc);
	memcpy(dfufake_staging.buf, head, WOZ_DFU_HDR_LEN);
	memcpy(dfufake_staging.buf + WOZ_DFU_PATCH_OFFSET, patch, patch_len);
}

/** Reset the doubles, stage a valid update, and return the applier entry point. */
static int apply_now(void)
{
	return logfake_sys_init_woz_dfu_apply();
}

#define APPLY_FROM_LEN 4096u
/* Must track CONFIG_WOZ_DFU_APPLIER_CHUNK, which the suite is compiled with. */
#define APPLY_CHUNK ((unsigned)CONFIG_WOZ_DFU_APPLIER_CHUNK)

static void prepare_apply(const uint8_t *patch, uint32_t patch_len)
{
	dfufake_reset();
	stage_for_apply(patch, patch_len, APPLY_FROM_LEN);
}

static void test_applier_gates(void)
{
	uint8_t patch[64];

	t_group("dfu applier gates");

	fill_pattern(patch, sizeof(patch), 13);

	/* A partition that will not open leaves the boot untouched. */
	dfufake_reset();
	dfufake_staging.fail_open = true;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("primary never opened", (long)dfufake_primary.open_calls, 0L);

	/* The normal-boot fast path: erased staging, one read, no erase. */
	dfufake_reset();
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("nothing erased on a normal boot", (long)dfufake_staging.erase_calls, 0L);
	T_EQ("staging closed", (long)dfufake_staging.close_calls, 1L);
	T_EQ("primary never opened", (long)dfufake_primary.open_calls, 0L);

	/* A header that cannot be read is treated as no header. */
	dfufake_reset();
	dfufake_staging.read_fail_in = 0;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("primary never opened", (long)dfufake_primary.open_calls, 0L);

	/* Every header field the bootloader re-checks, broken one at a time.
	 * Each must CONSUME the staging partition: leaving it would retry the
	 * same bad patch on every boot. */
	prepare_apply(patch, sizeof(patch));
	dfufake_staging.buf[4] = 0x7f; /* abi_version */
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("bad abi consumed the update", (long)dfufake_staging.erase_calls, 1L);
	T_EQ("primary never opened for a bad header", (long)dfufake_primary.open_calls, 0L);

	prepare_apply(patch, 0);
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("zero-length patch consumed", (long)dfufake_staging.erase_calls, 1L);

	prepare_apply(patch, sizeof(patch));
	{
		uint32_t huge = PATCH_MAX + 1u;

		memcpy(dfufake_staging.buf + 8, &huge, sizeof(huge)); /* patch_len */
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("oversized patch consumed", (long)dfufake_staging.erase_calls, 1L);
	}

	prepare_apply(patch, sizeof(patch));
	dfufake_staging.buf[WOZ_DFU_HDR_CRC_LEN] ^= 0x01u;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("bad header crc consumed", (long)dfufake_staging.erase_calls, 1L);

	/* A primary slot that will not open stops before anything is erased. */
	prepare_apply(patch, sizeof(patch));
	dfufake_primary.fail_open = true;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("nothing consumed when the primary is unavailable",
	     (long)dfufake_staging.erase_calls, 0L);

	/* The from-image gates, on a fresh apply. */
	prepare_apply(patch, sizeof(patch));
	{
		uint32_t past_end = DFUFAKE_PRIMARY_SIZE + 1u;

		memcpy(dfufake_staging.buf + 24, &past_end, sizeof(past_end)); /* from_len */
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("impossible from_len consumed", (long)dfufake_staging.erase_calls, 1L);
	}

	prepare_apply(patch, sizeof(patch));
	dfufake_staging.buf[WOZ_DFU_PATCH_OFFSET + 3] ^= 0x40u; /* a patch byte */
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("patch crc mismatch consumed", (long)dfufake_staging.erase_calls, 1L);
	T_EQ("detools never started", (long)dfufake.detools_init_calls, 0L);

	prepare_apply(patch, sizeof(patch));
	dfufake_primary.buf[10] ^= 0x40u; /* the from-image moved under us */
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("wrong from-image consumed", (long)dfufake_staging.erase_calls, 1L);
	T_EQ("detools never started", (long)dfufake.detools_init_calls, 0L);

	/* A read failure while CRC-ing the patch is a refusal, not a crash. */
	prepare_apply(patch, sizeof(patch));
	dfufake_staging.read_fail_in = 1; /* header read succeeds, the CRC read does not */
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("detools never started", (long)dfufake.detools_init_calls, 0L);
}

static void test_applier_apply(void)
{
	uint8_t patch[600]; /* larger than one CONFIG_WOZ_DFU_APPLIER_CHUNK */

	t_group("dfu applier");

	fill_pattern(patch, sizeof(patch), 17);

	/* The happy path, with a script that touches every callback and every
	 * shape of write the combiner has to cope with. */
	{
		const struct dfufake_op ops[] = {
			/* a page erase the applier passes straight through */
			{DFUFAKE_OP_ERASE, 0, DFUFAKE_PAGE_SIZE, 0, 0},
			/* three bytes: held, because flash takes whole words */
			{DFUFAKE_OP_WRITE, 0, 3, 0, 0x11},
			/* five more, contiguous: completes the first word */
			{DFUFAKE_OP_WRITE, 3, 5, 0, 0x22},
			/* somewhere else: the held tail must be padded out first */
			{DFUFAKE_OP_WRITE, 1000, 2, 0, 0x33},
			/* a read: everything buffered has to be in flash first */
			{DFUFAKE_OP_READ, 0, 8, 0, 0},
			/* detools erases what a segment covers; the last one is
			 * partial and only rounding up makes it legal */
			{DFUFAKE_OP_ERASE, DFUFAKE_PAGE_SIZE, 471, 0, 0},
			/* longer than the 128-byte combiner buffer */
			{DFUFAKE_OP_WRITE, 8192, 200, 0, 0x44},
			{DFUFAKE_OP_STEP_SET, 0, 0, 1, 0},
			{DFUFAKE_OP_STEP_SET, 0, 0, 2, 0},
			{DFUFAKE_OP_STEP_GET, 0, 0, 0, 0},
			{DFUFAKE_OP_STEP_SET, 0, 0, 0, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		T_EQ("apply returns 0", apply_now(), 0);

		T_EQ("every scripted operation accepted", (long)dfufake_ops_done(),
		     (long)ARRAY_SIZE(ops));
		T_EQ("detools initialised once", (long)dfufake.detools_init_calls, 1L);
		T_EQ("patch size handed to detools", (long)dfufake.detools_patch_size,
		     (long)sizeof(patch));
		T_EQ("whole patch streamed", (long)dfufake.detools_fed, (long)sizeof(patch));
		T_OK("streamed in more than one chunk", dfufake.detools_process_calls > 1u);
		T_EQ("finalized", (long)dfufake.detools_finalize_calls, 1L);

		/* The combined writes landed where they were addressed. */
		T_EQ("first write byte 0", (long)dfufake_peek(&dfufake_primary, 0), 0x11L);
		T_EQ("first write byte 2", (long)dfufake_peek(&dfufake_primary, 2), 0x11L);
		T_EQ("second write byte 3", (long)dfufake_peek(&dfufake_primary, 3), 0x22L);
		T_EQ("second write byte 7", (long)dfufake_peek(&dfufake_primary, 7), 0x22L);
		T_EQ("discontiguous write", (long)dfufake_peek(&dfufake_primary, 1000), 0x33L);
		T_EQ("long write head", (long)dfufake_peek(&dfufake_primary, 8192), 0x44L);
		T_EQ("long write tail", (long)dfufake_peek(&dfufake_primary, 8391), 0x44L);
		/* The tail of the discontiguous write was padded, not left stale. */
		T_EQ("pad after the discontiguous write",
		     (long)dfufake_peek(&dfufake_primary, 1002), 0xffL);

		/* The step log recorded two steps and was then cleared. */
		T_EQ("step_get saw both steps", dfufake_last_step_get(), 2);
		T_EQ("step log cleared", (long)dfufake_peek(&dfufake_staging, WOZ_DFU_STEP_OFFSET),
		     0xffL);

		/* Either way the update is consumed. */
		T_OK("staging consumed", dfufake_staging.erase_calls > 0u);
	}

	/* A resume skips the from-image check entirely: the slot is half
	 * patched by design and could not match from_crc32 any more. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_STEP_GET, 0, 0, 0, 0},
		};
		uint32_t recorded = 3u;

		prepare_apply(patch, sizeof(patch));
		/* Three completed steps in the log, and a from-image that no
		 * longer matches -- which is exactly what a resume looks like. */
		memcpy(dfufake_staging.buf + WOZ_DFU_STEP_OFFSET, &recorded, 4);
		memcpy(dfufake_staging.buf + WOZ_DFU_STEP_OFFSET + 4, &recorded, 4);
		memcpy(dfufake_staging.buf + WOZ_DFU_STEP_OFFSET + 8, &recorded, 4);
		dfufake_primary.buf[0] ^= 0xffu;
		dfufake_script(ops, ARRAY_SIZE(ops));

		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("resumed rather than rejected", (long)dfufake.detools_init_calls, 1L);
		T_EQ("step_get reported the completed count", dfufake_last_step_get(), 3);
	}

	/* detools failures are survivable and still consume the update: the
	 * slot is already damaged and retrying the same patch cannot help. */
	prepare_apply(patch, sizeof(patch));
	dfufake_script(NULL, 0);
	dfufake.detools_init_ret = -1;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("no chunk streamed after a failed init", (long)dfufake.detools_process_calls, 0L);
	T_OK("staging consumed", dfufake_staging.erase_calls > 0u);

	prepare_apply(patch, sizeof(patch));
	dfufake_script(NULL, 0);
	dfufake.detools_process_ret = -2;
	T_EQ("apply returns 0", apply_now(), 0);
	T_EQ("stopped at the first chunk", (long)dfufake.detools_process_calls, 1L);
	T_OK("staging consumed", dfufake_staging.erase_calls > 0u);

	/* A patch read failure mid-stream. */
	prepare_apply(patch, sizeof(patch));
	dfufake_script(NULL, 0);
	dfufake_staging.read_fail_in = 3;
	T_EQ("apply returns 0", apply_now(), 0);
	T_OK("staging consumed", dfufake_staging.erase_calls > 0u);
}

static void test_applier_callbacks(void)
{
	uint8_t patch[64];

	t_group("dfu applier callbacks");

	fill_pattern(patch, sizeof(patch), 19);

	/* Rounding the START of an erase down is never safe, so an unaligned
	 * start is a hard failure rather than something to paper over. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_ERASE, 100, DFUFAKE_PAGE_SIZE, 0, 0},
			{DFUFAKE_OP_WRITE, 0, 4, 0, 0x55},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("stopped at the unaligned erase", (long)dfufake_ops_done(), 1L);
	}

	/* An erase that would round past the end of the slot is refused. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_ERASE, DFUFAKE_PRIMARY_SIZE - DFUFAKE_PAGE_SIZE,
			 DFUFAKE_PAGE_SIZE + 1u, 0, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("stopped at the out-of-range erase", (long)dfufake_ops_done(), 1L);
	}

	/* A flash erase that fails is propagated, not swallowed. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_ERASE, 0, DFUFAKE_PAGE_SIZE, 0, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_primary.erase_fail_in = 0;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("stopped at the failing erase", (long)dfufake_ops_done(), 1L);
	}

	/* A write failure inside the combiner, and one in the padded tail. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_WRITE, 0, 200, 0, 0x66},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_primary.write_fail_in = 0;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("stopped at the failing write", (long)dfufake_ops_done(), 1L);
	}
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_WRITE, 0, 3, 0, 0x77}, /* held, no flush yet */
			{DFUFAKE_OP_READ, 0, 4, 0, 0},     /* forces the padded flush */
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_primary.write_fail_in = 0;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("stopped at the failing tail flush", (long)dfufake_ops_done(), 2L);
	}

	/* A read failure from the primary slot. The from-image CRC pass has
	 * already read the whole slot in chunks by the time a callback runs, so
	 * the knob has to step over exactly those reads to reach mem_read. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_READ, 0, 16, 0, 0},
		};
		const int crc_reads =
			(int)((APPLY_FROM_LEN + APPLY_CHUNK - 1u) / APPLY_CHUNK);

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_primary.read_fail_in = crc_reads;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("the from-image check still passed", (long)dfufake.detools_init_calls, 1L);
		T_EQ("stopped at the failing read", (long)dfufake_ops_done(), 1L);
	}

	/* Step numbers outside the log's range are refused; step 0 is not a
	 * step but a request to clear, and rejecting it would fail the patch at
	 * its very last moment. */
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_STEP_SET, 0, 0, -1, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("negative step refused", (long)dfufake_ops_done(), 1L);
	}
	{
		/* One page of words is the log's whole capacity. */
		const int too_many = (int)(WOZ_DFU_PAGE_SIZE / sizeof(uint32_t)) + 1;
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_STEP_SET, 0, 0, too_many, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("step past the log capacity refused", (long)dfufake_ops_done(), 1L);
	}
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_STEP_SET, 0, 0, 0, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_staging.erase_fail_in = 0;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("a failing step-log clear is reported", (long)dfufake_ops_done(), 1L);
	}
	{
		const struct dfufake_op ops[] = {
			{DFUFAKE_OP_STEP_SET, 0, 0, 1, 0},
		};

		prepare_apply(patch, sizeof(patch));
		dfufake_script(ops, ARRAY_SIZE(ops));
		dfufake_staging.write_fail_in = 0;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("a failing step write is reported", (long)dfufake_ops_done(), 1L);
	}

	/* step_get walks the log until it meets an erased word; a read failure
	 * part-way through is an error, not a short count. It runs BEFORE the
	 * CRC gates, so the second staging read is its first one -- and the
	 * partition is left alone rather than consumed, which is what separates
	 * this path from a failed integrity check. */
	{
		prepare_apply(patch, sizeof(patch));
		dfufake_script(NULL, 0);
		dfufake_staging.read_fail_in = 1;
		T_EQ("apply returns 0", apply_now(), 0);
		T_EQ("detools never started", (long)dfufake.detools_init_calls, 0L);
		T_EQ("an unreadable step log consumes nothing",
		     (long)dfufake_staging.erase_calls, 0L);
	}
}

/* ---- entry point ---------------------------------------------------------- */

/* The SMP image group drives the same receiver from CBOR; it lives in
 * test_dfu_smp.c and shares this binary because it calls into it. */
void test_dfu_smp(void);

int main(void)
{
	test_window();
	test_receiver_framing();
	test_receiver_auth();
	test_receiver_stage();
	test_receiver_integrity();
	test_receiver_upload();
	test_applier_gates();
	test_applier_apply();
	test_applier_callbacks();
	test_dfu_smp();

	if (t_fail > 0) {
		printf("  dfu: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	printf("  dfu: PASS (%d checks — RAM flash with real alignment rules, "
	       "real CRC-32, scripted detools, no crypto truth)\n",
	       t_pass);
	return 0;
}
