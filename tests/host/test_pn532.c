/** @file test_pn532.c — PN532 host-protocol driver against a scripted fake bus. */
#include <stdint.h>
#include <string.h>

#include "pn532.h"
#include "pn532_apdu.h"
#include "test.h"

/* ── fake bus ─────────────────────────────────────────────────────────── */

#define FB_MAX_WRITES 8
#define FB_MAX_REPLIES 8

static struct {
	uint8_t written[FB_MAX_WRITES][PN532_FRAME_BUF_SIZE];
	size_t written_len[FB_MAX_WRITES];
	int writes;
	const uint8_t *reply[FB_MAX_REPLIES];
	size_t reply_len[FB_MAX_REPLIES];
	int replies;
	int reply_at;
	int timeouts_left; /* wait_ready failures before becoming ready */
} fb;

static void fb_reset(void)
{
	memset(&fb, 0, sizeof(fb));
}

static void fb_push_reply(const uint8_t *frame, size_t len)
{
	fb.reply[fb.replies] = frame;
	fb.reply_len[fb.replies] = len;
	fb.replies++;
}

static int fb_write(void *ctx, const uint8_t *buf, size_t len)
{
	(void)ctx;
	if (fb.writes < FB_MAX_WRITES && len <= PN532_FRAME_BUF_SIZE) {
		memcpy(fb.written[fb.writes], buf, len);
		fb.written_len[fb.writes] = len;
	}
	fb.writes++;
	return PN532_OK;
}

static int fb_wait_ready(void *ctx, int timeout_ms)
{
	(void)ctx;
	(void)timeout_ms;
	if (fb.timeouts_left > 0) {
		fb.timeouts_left--;
		return PN532_ERR_TIMEOUT;
	}
	return PN532_OK;
}

static int fb_read(void *ctx, uint8_t *buf, size_t cap)
{
	(void)ctx;
	memset(buf, 0, cap);
	if (fb.reply_at >= fb.replies) {
		return PN532_ERR_IO;
	}
	size_t n = fb.reply_len[fb.reply_at];

	if (n > cap) {
		n = cap;
	}
	memcpy(buf, fb.reply[fb.reply_at], n);
	fb.reply_at++;
	return PN532_OK;
}

static const struct pn532_bus_ops fb_ops = {
	.write = fb_write,
	.wait_ready = fb_wait_ready,
	.read = fb_read,
};

static const uint8_t kAck[] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };

/* Build a chip→host response frame: PD0 = cmd + 1, payload follows. */
static size_t mk_resp(uint8_t cmd, const uint8_t *data, size_t n, uint8_t *out)
{
	const uint8_t len = (uint8_t)(2 + n);
	uint8_t dcs = (uint8_t)(0xD5 + (uint8_t)(cmd + 1));
	size_t at = 0;

	out[at++] = 0x00;
	out[at++] = 0x00;
	out[at++] = 0xFF;
	out[at++] = len;
	out[at++] = (uint8_t)(0x100 - len);
	out[at++] = 0xD5;
	out[at++] = (uint8_t)(cmd + 1);
	for (size_t i = 0; i < n; i++) {
		out[at++] = data[i];
		dcs = (uint8_t)(dcs + data[i]);
	}
	out[at++] = (uint8_t)(0x100 - dcs);
	out[at++] = 0x00;
	return at;
}

static struct pn532 mk_chip(void)
{
	struct pn532 p;

	fb_reset();
	pn532_init(&p, &fb_ops, NULL);
	return p;
}

/* ── suite ────────────────────────────────────────────────────────────── */

void test_pn532(void)
{
	uint8_t frame_a[64], frame_b[64], frame_c[64];
	size_t len_a, len_b;

	t_group("CRC_A");
	/* CRC-16/ISO-IEC-14443-3-A check value ("123456789" -> 0xBF05). */
	T_EQ("crc_a check value", pn532_crc_a((const uint8_t *)"123456789", 9), 0xBF05);

	t_group("command framing");
	{
		struct pn532 p = mk_chip();
		static const uint8_t fw[] = { 0x32, 0x01, 0x06, 0x07 };
		uint8_t out[4] = { 0 };

		len_a = mk_resp(0x02, fw, sizeof(fw), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("get_firmware rc", pn532_get_firmware_version(&p, out), PN532_OK);
		T_EQ("fw ic", out[0], 0x32);
		T_EQ("fw ver", out[1], 0x01);
		/* Exact host frame: 00 00 FF 02 FE D4 02 2A 00. */
		static const uint8_t expect[] = { 0x00, 0x00, 0xFF, 0x02, 0xFE,
						  0xD4, 0x02, 0x2A, 0x00 };
		T_EQ("frame len", fb.written_len[0], sizeof(expect));
		T_OK("frame bytes", memcmp(fb.written[0], expect, sizeof(expect)) == 0);
	}

	t_group("frame validation");
	{
		struct pn532 p = mk_chip();
		static const uint8_t fw[] = { 0x32, 0x01, 0x06, 0x07 };
		uint8_t out[4];

		len_a = mk_resp(0x02, fw, sizeof(fw), frame_a);
		frame_a[len_a - 2] ^= 0xFF; /* corrupt DCS */
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("bad DCS", pn532_get_firmware_version(&p, out), PN532_ERR_FRAME);

		p = mk_chip();
		static const uint8_t err_frame[] = { 0x00, 0x00, 0xFF, 0x01, 0xFF,
						     0x7F, 0x81, 0x00 };
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(err_frame, sizeof(err_frame));
		T_EQ("app error frame", pn532_get_firmware_version(&p, out), PN532_ERR_APP);

		p = mk_chip();
		fb.timeouts_left = 1;
		T_EQ("ready timeout", pn532_get_firmware_version(&p, out), PN532_ERR_TIMEOUT);

		p = mk_chip();
		T_EQ("params too large",
		     pn532_transact(&p, 0x40, frame_a, 254, NULL, 0, NULL, -1), PN532_ERR_SPACE);
	}

	t_group("InListPassiveTarget");
	{
		struct pn532 p = mk_chip();
		struct pn532_target tgt;
		static const uint8_t none[] = { 0x00 };

		len_a = mk_resp(0x4A, none, sizeof(none), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("no target", pn532_list_passive_target_106a(&p, &tgt, 100), 0);

		p = mk_chip();
		/* iPhone-shaped: SEL_RES 0x20 (ISO-DEP), 4-byte NFCID, 6-byte ATS. */
		static const uint8_t found[] = { 0x01, 0x01, 0x04, 0x03, 0x20,
						 0x04, 0x08, 0x77, 0x1B, 0x2A,
						 0x06, 0x75, 0x77, 0x81, 0x02, 0x80 };
		len_a = mk_resp(0x4A, found, sizeof(found), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("target found", pn532_list_passive_target_106a(&p, &tgt, 100), 1);
		T_EQ("tg", tgt.tg, 1);
		T_EQ("sel_res", tgt.sel_res, 0x20);
		T_OK("iso-dep", pn532_target_is_iso_dep(&tgt));
		T_EQ("nfcid len", tgt.nfcid_len, 4);
		T_EQ("nfcid[0]", tgt.nfcid[0], 0x08);
		T_EQ("ats len", tgt.ats_len, 6);
		T_EQ("ats tl", tgt.ats[0], 0x06);
	}

	t_group("InDataExchange");
	{
		struct pn532 p = mk_chip();
		static const uint8_t apdu[] = { 0x00, 0xA4, 0x04, 0x00 };
		static const uint8_t ok9000[] = { 0x00, 0x90, 0x00 };
		uint8_t rx[64];
		size_t rx_len = 0;

		len_a = mk_resp(0x40, ok9000, sizeof(ok9000), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("exchange rc",
		     pn532_in_data_exchange(&p, 1, apdu, sizeof(apdu), rx, sizeof(rx), &rx_len,
					    100),
		     PN532_OK);
		T_EQ("rx len", rx_len, 2);
		T_OK("rx 9000", rx[0] == 0x90 && rx[1] == 0x00);
		/* Frame data: ... D4 40 Tg APDU..., so Tg sits at offset 7. */
		T_EQ("tg byte", fb.written[0][7], 0x01);
		T_OK("apdu copied", memcmp(&fb.written[0][8], apdu, sizeof(apdu)) == 0);
	}

	t_group("InDataExchange chained response (MI)");
	{
		struct pn532 p = mk_chip();
		static const uint8_t apdu[] = { 0x00, 0xB0, 0x00, 0x00 };
		static const uint8_t part1[] = { 0x40, 'A', 'B' }; /* status MI + data */
		static const uint8_t part2[] = { 0x00, 'C', 0x90, 0x00 };
		uint8_t rx[64];
		size_t rx_len = 0;

		len_a = mk_resp(0x40, part1, sizeof(part1), frame_a);
		len_b = mk_resp(0x40, part2, sizeof(part2), frame_b);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_b, len_b);
		T_EQ("chained rc",
		     pn532_in_data_exchange(&p, 1, apdu, sizeof(apdu), rx, sizeof(rx), &rx_len,
					    100),
		     PN532_OK);
		T_EQ("chained rx len", rx_len, 5);
		T_OK("chained rx", memcmp(rx, "ABC\x90\x00", 5) == 0);
		/* Continuation request carries the bare Tg: D4 40 01 (10-byte frame). */
		T_EQ("cont writes", fb.writes, 2);
		T_EQ("cont frame len", fb.written_len[1], 10);
		T_EQ("cont tg", fb.written[1][7], 0x01);
	}

	t_group("InDataExchange chained request (MI out)");
	{
		struct pn532 p = mk_chip();
		static uint8_t big[PN532_XFER_CHUNK + 60];
		static const uint8_t accepted[] = { 0x00 };
		static const uint8_t done[] = { 0x00, 0x90, 0x00 };
		uint8_t rx[64];
		size_t rx_len = 0;

		for (size_t i = 0; i < sizeof(big); i++) {
			big[i] = (uint8_t)i;
		}
		len_a = mk_resp(0x40, accepted, sizeof(accepted), frame_a);
		len_b = mk_resp(0x40, done, sizeof(done), frame_b);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_b, len_b);
		T_EQ("big rc",
		     pn532_in_data_exchange(&p, 1, big, sizeof(big), rx, sizeof(rx), &rx_len, 100),
		     PN532_OK);
		T_EQ("big writes", fb.writes, 2);
		T_EQ("first tg has MI", fb.written[0][7], 0x41);
		T_EQ("second tg no MI", fb.written[1][7], 0x01);
		T_EQ("big rx len", rx_len, 2);

		p = mk_chip();
		static const uint8_t rf_timeout[] = { 0x01 };
		len_a = mk_resp(0x40, rf_timeout, sizeof(rf_timeout), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("status error rc",
		     pn532_in_data_exchange(&p, 1, big, 4, rx, sizeof(rx), &rx_len, 100),
		     PN532_ERR_STATUS);
		T_EQ("status value", p.last_status, 0x01);
	}

	t_group("extended frame + CommunicateThru");
	{
		struct pn532 p = mk_chip();
		uint8_t rx[16];
		size_t rx_len = 0;
		/* Extended response: 00 00 FF FF FF LENM LENL LCS D5 43 st X Y DCS 00. */
		size_t at = 0;

		frame_c[at++] = 0x00;
		frame_c[at++] = 0x00;
		frame_c[at++] = 0xFF;
		frame_c[at++] = 0xFF;
		frame_c[at++] = 0xFF;
		frame_c[at++] = 0x00; /* LENM */
		frame_c[at++] = 0x05; /* LENL: TFI + PD0 + 3 data */
		frame_c[at++] = 0xFB; /* LCS */
		frame_c[at++] = 0xD5;
		frame_c[at++] = 0x43;
		frame_c[at++] = 0x00; /* status OK */
		frame_c[at++] = 'X';
		frame_c[at++] = 'Y';
		uint8_t dcs = (uint8_t)(0xD5 + 0x43 + 0x00 + 'X' + 'Y');

		frame_c[at++] = (uint8_t)(0x100 - dcs);
		frame_c[at++] = 0x00;
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_c, at);
		static const uint8_t ecp[] = { 0x6A, 0x02 };
		T_EQ("comm_thru rc",
		     pn532_comm_thru(&p, ecp, sizeof(ecp), rx, sizeof(rx), &rx_len, 100),
		     PN532_OK);
		T_EQ("comm_thru rx len", rx_len, 2);
		T_OK("comm_thru rx", rx[0] == 'X' && rx[1] == 'Y');

		/* No answer to a broadcast: status 0x01, surfaced as ERR_STATUS. */
		p = mk_chip();
		static const uint8_t timeout_status[] = { 0x01 };
		len_a = mk_resp(0x42, timeout_status, sizeof(timeout_status), frame_a);
		fb_push_reply(kAck, sizeof(kAck));
		fb_push_reply(frame_a, len_a);
		T_EQ("broadcast rc",
		     pn532_comm_thru(&p, ecp, sizeof(ecp), NULL, 0, NULL, 100),
		     PN532_ERR_STATUS);
		T_EQ("broadcast status", p.last_status, 0x01);
	}

	t_group("PN532 APDU transport adaptation");
	{
		struct woz_pn532_apdu_plan plan;
		uint8_t wire[512];
		size_t wire_len = 0;
		bool more = false;

		static const uint8_t select[] = {
			0x00, 0xa4, 0x04, 0x00, 0x02, 0x12, 0x34, 0x00,
		};
		T_EQ("plan SELECT passthrough",
		     woz_pn532_apdu_plan_init(select, sizeof(select), &plan), 0);
		T_OK("SELECT not adapted", !plan.adapted);
		T_EQ("emit SELECT", woz_pn532_apdu_plan_next(&plan, wire, sizeof(wire),
			&wire_len, &more), 0);
		T_OK("SELECT unchanged", !more && wire_len == sizeof(select) &&
			memcmp(wire, select, sizeof(select)) == 0);

		static const uint8_t get_short[] = { 0x00, 0xc0, 0x00, 0x00, 0x00 };
		T_EQ("plan short GET RESPONSE",
		     woz_pn532_apdu_plan_init(get_short, sizeof(get_short), &plan), 0);
		T_OK("short GET RESPONSE adapted", plan.adapted);
		T_EQ("emit short GET RESPONSE", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("short GET RESPONSE Le=240", wire_len == 5 && wire[4] == 0xf0);

		static const uint8_t get_extended[] = {
			0x00, 0xc0, 0x00, 0x00, 0x00, 0x0e, 0x00,
		};
		T_EQ("plan extended GET RESPONSE",
		     woz_pn532_apdu_plan_init(get_extended, sizeof(get_extended), &plan), 0);
		T_OK("extended GET RESPONSE adapted", plan.adapted);
		T_EQ("emit extended GET RESPONSE", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("extended GET RESPONSE Le=240", wire_len == 7 &&
			wire[5] == 0x00 && wire[6] == 0xf0);

		static const uint8_t short_envelope[] = {
			0x00, 0xc3, 0x00, 0x00, 0x04, 't', 'e', 's', 't', 0x00,
		};
		T_EQ("plan short ENVELOPE",
		     woz_pn532_apdu_plan_init(short_envelope, sizeof(short_envelope), &plan), 0);
		T_OK("short ENVELOPE response adapted", plan.adapted);
		T_EQ("emit short ENVELOPE", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("short ENVELOPE preserves data and clamps Le",
			!more && wire_len == sizeof(short_envelope) &&
			memcmp(wire, short_envelope, sizeof(short_envelope) - 1) == 0 &&
			wire[sizeof(short_envelope) - 1] == 0xf0);

		uint8_t envelope[512] = { 0x00, 0xc3, 0x00, 0x00, 0x00, 0x01, 0xf4 };
		for (size_t i = 0; i < 500; ++i) envelope[7 + i] = (uint8_t)i;
		envelope[507] = 0x0e;
		envelope[508] = 0x00;
		T_EQ("plan large extended ENVELOPE",
		     woz_pn532_apdu_plan_init(envelope, 509, &plan), 0);
		T_OK("large ENVELOPE adapted", plan.adapted);

		T_EQ("emit ENVELOPE fragment 1", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("fragment 1 shape", more && wire_len == 238 && wire[0] == 0x10 &&
			wire[4] == 0x00 && wire[5] == 0x00 && wire[6] == 0xe7 &&
			memcmp(wire + 7, envelope + 7, 231) == 0);

		T_EQ("emit ENVELOPE fragment 2", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("fragment 2 shape", more && wire_len == 238 && wire[0] == 0x10 &&
			memcmp(wire + 7, envelope + 7 + 231, 231) == 0);

		T_EQ("emit ENVELOPE fragment 3", woz_pn532_apdu_plan_next(&plan, wire,
			sizeof(wire), &wire_len, &more), 0);
		T_OK("fragment 3 shape", !more && wire_len == 47 && wire[0] == 0x00 &&
			wire[4] == 0x00 && wire[5] == 0x00 && wire[6] == 0x26 &&
			memcmp(wire + 7, envelope + 7 + 462, 38) == 0 &&
			wire[45] == 0x00 && wire[46] == 0xf0);
	}
}
