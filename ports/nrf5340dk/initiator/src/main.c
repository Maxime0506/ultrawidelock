// nRF5340 DK application entry for the Aliro initiator, the User-Device role that
// stands in for an iPhone on the bench. Starts the Zephyr BLE central, which
// scans for the reader's 0xFFF2 advert, connects, reads the reader's SPSM,
// supported versions and features, writes the version it selects, and opens the
// L2CAP channel. It then runs the Access Protocol over that channel: every
// inbound AUTH0/AUTH1/EXCHANGE command is fed to the device state machine and the
// sealed response is framed straight back, ending in the same 32-byte URSK the
// reader derives. Credentials are the compiled-in bench pair below, which works
// only against a reader running its dev identity with an empty trust store.
/*
 * Port of ports/esp32/apps/initiator/main/main.c -- the same program on two
 * stacks, differing only in logging macros and entry point (aliro_ble_central.h
 * absorbs NimBLE-vs-Zephyr); keep them in step. The transport half lives in
 * firmware/src/aliro_ble_central_zephyr.c, the protocol half in woz_aliro's
 * aliro_device; this file is only the glue. UWB is NOT yet wired: ESTABLISHED
 * means the boards agreed a URSK, ranging on it needs the DWM3000EVB driver.
 */
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aliro_apdu.h" /* 4-byte envelope: frame/unframe + PROTO/OP constants */
#include "aliro_ble_central.h"
#include "aliro_crypto.h" /* aliro_crypto_init: brings up the PSA backend */
#include "aliro_device.h"
#include "ranging.h" /* the post-auth BleSK ranging-setup driver */

#if defined(CONFIG_WOZ_UWB)
#include "uwb_min.h"
#if defined(CONFIG_SOC_SERIES_NRF53)
#include <hal/nrf_clock.h>
#include <nrfx.h>
#include <zephyr/init.h>

/**
 * Put HFCLK on DIV_1 before anything else starts, so SPIM4 samples MISO correctly.
 *
 * Measured on this bench 2026-08-07. With HFCLKCTRL at DIV_2, every DW3000 read
 * comes back sampled one SPI clock early: DEV_ID 0xdeca0302 arrives as
 * 0x6fe50101, which reconstructs exactly under a one-bit right shift with
 * byte-to-byte carry. Setting DIV_1 turns the same read good on the same board
 * with nothing else changed, so the divider is causal rather than correlated.
 * SPIM4's sample point is derived from HFCLK; spi_nrfx_spim.c:182-184 reads this
 * same divider for the same reason.
 *
 * Why this is not inherited: nrf_qspi_nor.c is the ONLY caller of
 * nrf_clock_hfclk_div_set() in Zephyr and NCS combined. The door lock reads the
 * DW3000 correctly purely because it builds CONFIG_NORDIC_QSPI_NOR for its
 * external flash and that driver leaves the divider on DIV_1. This application
 * has no external flash and must therefore own the divider itself. Do not
 * "clean this up" by deleting it because nothing appears to use it.
 *
 * PRE_KERNEL_1 so it lands before the SPI driver, and well before BLE and MPSL,
 * which is the reason it is here and not in radio_probe(): changing a global
 * clock underneath a running radio is how you buy intermittent faults.
 */
static int hfclk_div_fixup(void)
{
	nrf_clock_hfclk_div_set(NRF_CLOCK, NRF_CLOCK_HFCLK_DIV_1);
	return 0;
}
SYS_INIT(hfclk_div_fixup, PRE_KERNEL_1, 0);
#endif
#endif

LOG_MODULE_REGISTER(initiator, CONFIG_LOG_DEFAULT_LEVEL);

/* Aliro protocol v1.0 — the only version our reader offers or we speak. */
#define INITIATOR_VERSION 0x0100u

/* The largest Access-Protocol response we build. AUTH1Response is the big one
 * (65-byte device public key + 64-byte signature + TLV overhead) and lands well
 * under this; the reader sizes its own command buffer at 256 for the same
 * reason. */
#define INITIATOR_RESP_MAX 320u

/* An enrolled build gets its identity from the header scripts/aliro-enroll.py
 * writes: the real reader's public identity read back over Matter, plus a
 * credential this initiator was issued by the reader's home. That is the
 * high-fidelity configuration -- the reader is a normal provisioned lock and we
 * are a normal enrolled device. The header is gitignored because it carries a
 * private key, so its absence is the ordinary case and must not break the build.
 *
 * CMakeLists.txt decides which way this goes, by looking for the file. It is NOT
 * __has_include: that construct is unparseable to semgrep, and a file it cannot
 * parse is a file NONE of its rules run against -- a silent loss of SAST
 * coverage over the one translation unit here that handles key material. The
 * cost is that adding the header needs a re-configure, not just a rebuild.
 */
#ifdef ALIRO_BENCH_ENROLLED
#include "bench_identity.h"
#endif

#ifndef ALIRO_BENCH_ENROLLED
/*
 * ---- bench credentials -----------------------------------------------------
 *
 * Fallback for an un-enrolled bench: only works against a reader on its DEV
 * identity. Run scripts/aliro-enroll.py to target a real provisioned reader.
 *
 * The reader identity below is the DEV identity every unprovisioned reader falls
 * back to (aliro_prov.c, aliro_prov_dev_default). reader_id is that file's
 * k_dev_reader_id verbatim, and the verification key is the public point of its
 * k_dev_sign_priv — note reader_id is exactly that point's X coordinate, which
 * is how the dev identity was constructed and a free check that these two agree.
 *
 * This only works against a reader that is still on its dev identity AND has an
 * empty trust store: that combination makes the reader accept any presented
 * credential (aliro_reader.c, the tv == 1 && s_id.is_dev branch). A reader
 * provisioned over Matter has a real identity and enforces its trust store, so
 * these constants are wrong for it and the transaction will fail at AUTH1.
 *
 * The credential keypair is a throwaway generated for the bench. It is a real
 * P-256 key and it is in a public repository, so it is a test fixture and
 * nothing else: never provision it into anything that guards a door.
 */
static const uint8_t k_reader_id[32] = {
	0x11, 0x3b, 0x1a, 0x9e, 0xf2, 0x95, 0x67, 0x60, 0x8b, 0x75, 0x00,
	0xfb, 0xac, 0xa6, 0x09, 0xe9, 0xc0, 0x7b, 0x87, 0x4e, 0x18, 0x2a,
	0xe5, 0x65, 0x02, 0x4b, 0x54, 0x3e, 0x3b, 0x40, 0x93, 0x5f,
};
static const uint8_t k_reader_verif_pub[65] = {
	0x04, 0x11, 0x3b, 0x1a, 0x9e, 0xf2, 0x95, 0x67, 0x60, 0x8b, 0x75, 0x00, 0xfb,
	0xac, 0xa6, 0x09, 0xe9, 0xc0, 0x7b, 0x87, 0x4e, 0x18, 0x2a, 0xe5, 0x65, 0x02,
	0x4b, 0x54, 0x3e, 0x3b, 0x40, 0x93, 0x5f, 0xe2, 0x96, 0x5c, 0x08, 0x3e, 0xac,
	0x2c, 0x96, 0xf0, 0x49, 0x5b, 0x0c, 0x3c, 0x6b, 0x09, 0x12, 0x15, 0xca, 0x3d,
	0x40, 0x6f, 0x82, 0x14, 0x1b, 0xfd, 0x18, 0x66, 0xf4, 0xad, 0x1e, 0x8c, 0xd4,
};
static const uint8_t k_cred_priv[32] = {
	0x33, 0xbb, 0x36, 0x4e, 0xf0, 0xf2, 0xee, 0xfe, 0xdc, 0xd3, 0x22,
	0x8a, 0xe8, 0x57, 0x2f, 0xc2, 0xe5, 0xc9, 0x02, 0x4a, 0x40, 0xc5,
	0xe5, 0x78, 0xcb, 0xa2, 0xa6, 0x93, 0x8b, 0x29, 0xc3, 0xb5,
};
#endif /* ALIRO_BENCH_ENROLLED */

/* One transaction at a time: the reader serves one Aliro session per connection
 * and the central connects to one reader. s_conn is the connection s_dev belongs
 * to, so a stale SDU after a reconnect cannot be fed to the wrong session. */
static struct aliro_device s_dev;
static uint16_t s_conn;
static bool s_armed;

/**
 * Return a human-readable string for an Aliro device phase: IDLE, SENT_AUTH0_RESP, SENT_AUTH1_RESP,
 * ESTABLISHED, or FAILED.
 */
static const char *phase_str(enum aliro_device_phase p)
{
	switch (p) {
	case ALIRO_DEV_IDLE:
		return "IDLE";
	case ALIRO_DEV_SENT_AUTH0_RESP:
		return "SENT_AUTH0_RESP";
	case ALIRO_DEV_SENT_AUTH1_RESP:
		return "SENT_AUTH1_RESP";
	case ALIRO_DEV_ESTABLISHED:
		return "ESTABLISHED";
	default:
		return "FAILED";
	}
}

/**
 * Callback when the BLE transport is ready after the peer advertises its SPSM and supported
 * versions. Initialize the device state machine with the credential private key and reader
 * identity, derive the BleSK salt from the peer's published version list, and arm the device to
 * wait for AUTH0. Log connection and version details.
 */
static void on_ready(uint16_t conn_handle, const struct aliro_ble_central_peer *peer)
{
	LOG_INF("=== transport up (conn %u) ===", conn_handle);
	LOG_INF("  SPSM     0x%04x", (unsigned)peer->spsm);
	LOG_INF("  features 0x%02x", peer->features);
	for (size_t i = 0; i < peer->versions_count; i++) {
		LOG_INF("  version[%u] 0x%04x", (unsigned)i, (unsigned)peer->versions[i]);
	}

	s_armed = false;
	if (aliro_device_init(&s_dev, k_cred_priv, k_reader_id, k_reader_verif_pub) != 0) {
		LOG_ERR("device init failed (EC unavailable?)");
		return;
	}

	/* The peer's published version list is the BleSK salt (§11.8.1), and it is a
	 * property of the reader, not of us: our ESP32 reader publishes v1.0 alone
	 * while the nRF publishes two entries. Taking it from the GATT READ we just
	 * did is the whole reason that READ happens before the channel opens; the
	 * compiled-in default would derive a BleSK the nRF does not share. */
	uint8_t salt[2u * (ALIRO_BLE_CENTRAL_MAX_VERSIONS + 1u)];
	size_t salt_len = 0;

	if (aliro_ble_central_blesk_salt(peer, INITIATOR_VERSION, salt, sizeof(salt), &salt_len) !=
	    0) {
		LOG_ERR("BleSK salt assembly failed");
		return;
	}
	if (aliro_device_set_blesk_salt(&s_dev, salt, salt_len) != 0) {
		LOG_ERR("BleSK salt rejected (%u B)", (unsigned)salt_len);
		return;
	}
	LOG_HEXDUMP_INF(salt, salt_len, "blesk salt");

	s_conn = conn_handle;
	s_armed = true;

	/*
	 * The DEVICE speaks first. The reader sits in PH_IDLE until it receives
	 * a NOTIFICATION/Initiate-Access-Protocol frame, and start_auth() --
	 * which is what emits AUTH0 -- has exactly one call site, that branch
	 * (aliro_reader.c:1529). Without this both ends wait for each other: the
	 * L2CAP channel opens, the reader logs "Aliro session created", and
	 * nothing else ever happens. Observed on hardware, twice, before this
	 * line existed.
	 *
	 * The payload is the 0xA5 proprietary-info TLV that becomes the trailing
	 * field of the session-key salt (Aliro Table 10-2). A real phone sends
	 * its own; this is the CSA-app v1.0-only value, byte-identical to the
	 * fallback the reader substitutes when a device sends none -- so the two
	 * ends derive the same salt whether or not this frame carries it, and
	 * the reader logs which of the two happened.
	 */
	{
		static const uint8_t a5_csa_v1[] = {
			0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00,
		};
		uint8_t frame[ALIRO_ENVELOPE_HDR + sizeof(a5_csa_v1)];
		size_t frame_len = 0;
		int rc;

		if (aliro_ble_frame(ALIRO_PROTO_NOTIFICATION, ALIRO_NOTIF_INITIATE_AP, a5_csa_v1,
				    sizeof(a5_csa_v1), frame, sizeof(frame), &frame_len) != 0) {
			LOG_ERR("Initiate-AP framing failed");
			return;
		}
		rc = aliro_ble_central_send(conn_handle, frame, frame_len);
		LOG_INF("device armed; sent Initiate-AP (%u B, rc=%d), expecting AUTH0",
			(unsigned)frame_len, rc);
	}
}

static void on_data(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	uint8_t type, opcode;
	const uint8_t *pl;
	size_t pl_len;

	/* One envelope per SDU. Our reader sends exactly that (send_ap_command frames
	 * and sends one at a time), so this is right against it; the reader's own RX
	 * path splits coalesced envelopes because a real iPhone packs several into one
	 * SDU. If this is ever pointed at a reader that does the same, the trailing
	 * envelopes are dropped here and the transaction stalls. */
	if (aliro_ble_unframe(data, len, &type, &opcode, &pl, &pl_len) != 0) {
		LOG_WRN("conn %u: not a valid envelope (%u B)", conn_handle, (unsigned)len);
		LOG_HEXDUMP_WRN(data, len, "sdu");
		return;
	}

	/* Once ESTABLISHED the reader stops speaking Access Protocol and starts
	 * sending BleSK-sealed SDUs on the ranging channel, beginning with
	 * Reader-Status Access-Protocol-Completed. Those go to ranging.c.
	 *
	 * It gets the WHOLE SDU, not the unframed payload: a sealed SDU already is
	 * [proto][id][len_be16][ct||tag], so the four bytes unframed above are its
	 * own authenticated header, not an envelope wrapped around it. They were
	 * only unframed here because the two framings are the same shape. */
	if (type != ALIRO_PROTO_ACCESS || opcode != ALIRO_AP_OP_COMMAND) {
		if (initiator_ranging_on_sdu(conn_handle, data, len) == 0) {
			return;
		}
		LOG_INF("conn %u: non-AP SDU type=0x%02x op=0x%02x (%u B), no consumer",
			conn_handle, type, opcode, (unsigned)pl_len);
		LOG_HEXDUMP_INF(pl, pl_len, "payload");
		return;
	}

	if (!s_armed || conn_handle != s_conn) {
		LOG_WRN("conn %u: AP command with no armed session", conn_handle);
		return;
	}

	uint8_t resp[INITIATOR_RESP_MAX];
	size_t resp_len = 0;

	if (aliro_device_on_command(&s_dev, pl, pl_len, resp, sizeof(resp), &resp_len) != 0) {
		LOG_ERR("conn %u: command rejected in phase %s", conn_handle,
			phase_str(s_dev.phase));
		LOG_HEXDUMP_ERR(pl, pl_len, "command");
		s_armed = false;
		return;
	}

	uint8_t frame[ALIRO_ENVELOPE_HDR + INITIATOR_RESP_MAX];
	size_t frame_len = 0;

	if (aliro_ble_frame(ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, resp, resp_len, frame,
			    sizeof(frame), &frame_len) != 0) {
		LOG_ERR("conn %u: response framing failed (%u B)", conn_handle, (unsigned)resp_len);
		return;
	}

	int rc = aliro_ble_central_send(conn_handle, frame, frame_len);

	LOG_INF("conn %u: -> %u B response, phase %s (send rc=%d)", conn_handle, (unsigned)resp_len,
		phase_str(s_dev.phase), rc);

	if (s_dev.phase == ALIRO_DEV_ESTABLISHED) {
		LOG_INF("=== ESTABLISHED: URSK agreed with the reader ===");
		LOG_HEXDUMP_INF(s_dev.ursk, sizeof(s_dev.ursk), "ursk");
		/* sc_ble is keyed by the same derivation that produced the URSK, so the
		 * ranging channel is usable from this line onward and not before. */
		initiator_ranging_begin(&s_dev, conn_handle);
	}
}

static void on_closed(uint16_t conn_handle)
{
	LOG_WRN("transport closed (conn %u, phase %s)", conn_handle,
		s_armed ? phase_str(s_dev.phase) : "unarmed");
	/* The URSK and both channel keys die with the connection; zero them rather
	 * than leave a live session key in RAM for the next walk-up to trip over. */
	initiator_ranging_end(conn_handle);
	if (conn_handle == s_conn) {
		memset(&s_dev, 0, sizeof(s_dev));
		s_armed = false;
	}
}

#if defined(CONFIG_WOZ_UWB)
/**
 * Bring up the DWM3000EVB and report its DEV_ID.
 *
 * The first step of Stage 1b part two, and on its own it proves only that the
 * seated shield answers over SPI from THIS application: the reader's own probe
 * runs inside a Matter door lock, so nothing until now showed the radio coming up
 * beside a BLE central. Never fatal. A missing or unseated shield must not stop
 * the board doing the BLE half, which is the part that works.
 */
static void radio_probe(void)
{
	uint32_t id = 0;
	int rc = uwb_min_radio_init();

	if (rc != 0) {
		LOG_ERR("DW3000 init failed (rc=%d); is the DWM3000EVB seated?", rc);
		return;
	}
	if (uwb_min_read_chipid(&id) != 0) {
		LOG_ERR("DW3000 DEV_ID read failed");
		return;
	}
	LOG_INF("DW3000 raw DEV_ID = 0x%08x (expect 0x%08x)", (unsigned)id,
		(unsigned)UWB_DW3110_DEV_ID);
}
#else
static void radio_probe(void)
{
}
#endif

/**
 * Initialize the Aliro BLE central stack, register event callbacks for ready/data/closed, bring up
 * the PSA crypto backend, and begin scanning for an Aliro reader.
 */
int main(void)
{
	struct aliro_ble_central_config cfg;

	/* Who we CONNECT to and who we AUTHENTICATE as are deliberately separate
	 * here. cfg.reader_id is left all-zero, which makes the transport latch onto
	 * the first Aliro reader it sees and log its group id; the crypto identity is
	 * k_reader_id above. Filling cfg.reader_id in would be the correct behaviour
	 * for a real device, but on the bench it turns "the reader was provisioned
	 * over Matter, so the dev constants are wrong" into an initiator that scans
	 * forever in silence. Latching instead gets us to a legible AUTH1 rejection
	 * that names the phase it died in. */
	memset(&cfg, 0, sizeof(cfg));
	cfg.selected_version = INITIATOR_VERSION;
	cfg.cb.on_ready = on_ready;
	cfg.cb.on_data = on_data;
	cfg.cb.on_closed = on_closed;

	/* Bring up the PSA backend before anything can reach for a key. The reader
	 * does this inside aliro_reader_start (aliro_reader.c:1447); the initiator has
	 * no equivalent entry point, so it belongs here. Without it aliro_device_init
	 * fails in on_ready, which is a confusing place to discover it. */
	if (aliro_crypto_init() != 0) {
		LOG_ERR("PSA crypto init failed");
		return 0;
	}

	/* Before BLE, so the radio's verdict is the first thing in the log rather
	 * than something to hunt for after a walk-up. Moving it later was tried and
	 * reverted: probing at ~258 ms and at 2 s after boot read the same bytes, but
	 * both runs were against a badly seated shield, so that comparison shows only
	 * that the fault was constant, not that the timing is safe. If a probe ever
	 * fails here, reseat the DWM3000EVB before touching this call site. */
	radio_probe();

	if (aliro_ble_central_start(&cfg) != 0) {
		LOG_ERR("BLE central start failed");
		return 0;
	}
	LOG_INF("Aliro initiator up; scanning for a reader");
	/* Which identity is compiled in decides which readers can possibly answer,
	 * and getting that wrong looks identical to a transport fault from the log. */
#ifdef ALIRO_BENCH_ENROLLED
	LOG_INF("identity: ENROLLED (scripts/aliro-enroll.py)");
#else
	LOG_INF("identity: DEV fallback -- only an unprovisioned reader will accept us");
#endif

	/* Returning is correct on Zephyr, unlike the ESP-IDF app's forever loop: the
	 * whole chain from here runs on the BT RX thread's callbacks, and main's stack
	 * is reclaimed rather than parked in a sleep. */
	return 0;
}
