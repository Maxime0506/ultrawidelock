// ESP32-S3 application entry for the Aliro initiator, the User-Device role that
// stands in for an iPhone on the bench. Starts the NimBLE central, which scans
// for the reader's 0xFFF2 advert, connects, reads the reader's SPSM, supported
// versions and features, writes the version it selects, and opens the L2CAP
// channel. It then runs the Access Protocol over that channel: every inbound
// AUTH0/AUTH1/EXCHANGE command is fed to the device state machine and the sealed
// response is framed straight back, ending in the same 32-byte URSK the reader
// derives. Credentials are the compiled-in bench pair below, which works only
// against a reader running its dev identity with an empty trust store.
/*
 * Aliro initiator (User-Device role) — Stage 1a: BLE transport + Access Protocol.
 *
 * The transport half (scan/connect/discover/READ/WRITE/CoC) lives in the
 * aliro_ble_central component; the protocol half is modules/woz_aliro's
 * aliro_device, host-tested and byte-anchored to the spec. This file is only the
 * glue: hand the peer's real version list to the BleSK salt, unwrap the 4-byte
 * L2CAP envelope, and pump commands through aliro_device_on_command.
 *
 * NOT yet wired: UWB. Reaching ESTABLISHED means the two boards agreed a URSK;
 * ranging on it is Stage 1b and needs a DWM3000 on this board.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aliro_apdu.h" /* 4-byte envelope: frame/unframe + PROTO/OP constants */
#include "aliro_ble_central.h"
#include "aliro_crypto.h" /* aliro_crypto_init: brings up the PSA backend */
#include <openaliro/device.h>

static const char *TAG = "initiator";

/* Aliro protocol v1.0 — the only version our reader offers or we speak. */
#define INITIATOR_VERSION 0x0100u

/* The largest Access-Protocol response we build. AUTH1Response is the big one
 * (65-byte device public key + 64-byte signature + TLV overhead) and lands well
 * under this; the reader sizes its own command buffer at 256 for the same
 * reason. */
#define INITIATOR_RESP_MAX 320u

/*
 * ---- bench credentials -----------------------------------------------------
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
	ESP_LOGI(TAG, "=== transport up (conn %u) ===", conn_handle);
	ESP_LOGI(TAG, "  SPSM     0x%04x", (unsigned)peer->spsm);
	ESP_LOGI(TAG, "  features 0x%02x", peer->features);
	for (size_t i = 0; i < peer->versions_count; i++) {
		ESP_LOGI(TAG, "  version[%u] 0x%04x", (unsigned)i, (unsigned)peer->versions[i]);
	}

	s_armed = false;
	if (aliro_device_init(&s_dev, k_cred_priv, k_reader_id, k_reader_verif_pub) != 0) {
		ESP_LOGE(TAG, "device init failed (EC unavailable?)");
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
		ESP_LOGE(TAG, "BleSK salt assembly failed");
		return;
	}
	if (aliro_device_set_blesk_salt(&s_dev, salt, salt_len) != 0) {
		ESP_LOGE(TAG, "BleSK salt rejected (%u B)", (unsigned)salt_len);
		return;
	}
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, salt, salt_len, ESP_LOG_INFO);

	s_conn = conn_handle;
	s_armed = true;
	ESP_LOGI(TAG, "device armed; waiting for the reader to send AUTH0");
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
		ESP_LOGW(TAG, "conn %u: not a valid envelope (%u B)", conn_handle, (unsigned)len);
		ESP_LOG_BUFFER_HEX(TAG, data, len);
		return;
	}

	/* Anything that is not an Access-Protocol command is dumped, not dropped
	 * silently: once ESTABLISHED the reader also sends BleSK-sealed ranging SDUs
	 * (proto 2/3), and seeing them arrive is how Stage 1b will know where to
	 * start. We have no ranging channel consumer yet. */
	if (type != ALIRO_PROTO_ACCESS || opcode != ALIRO_AP_OP_COMMAND) {
		ESP_LOGI(TAG, "conn %u: non-AP SDU type=0x%02x op=0x%02x (%u B)", conn_handle, type,
			 opcode, (unsigned)pl_len);
		ESP_LOG_BUFFER_HEX(TAG, pl, pl_len);
		return;
	}

	if (!s_armed || conn_handle != s_conn) {
		ESP_LOGW(TAG, "conn %u: AP command with no armed session", conn_handle);
		return;
	}

	uint8_t resp[INITIATOR_RESP_MAX];
	size_t resp_len = 0;

	if (aliro_device_on_command(&s_dev, pl, pl_len, resp, sizeof(resp), &resp_len) != 0) {
		ESP_LOGE(TAG, "conn %u: command rejected in phase %s", conn_handle,
			 phase_str(s_dev.phase));
		ESP_LOG_BUFFER_HEX(TAG, pl, pl_len);
		s_armed = false;
		return;
	}

	uint8_t frame[ALIRO_ENVELOPE_HDR + INITIATOR_RESP_MAX];
	size_t frame_len = 0;

	if (aliro_ble_frame(ALIRO_PROTO_ACCESS, ALIRO_AP_OP_RESPONSE, resp, resp_len, frame,
			    sizeof(frame), &frame_len) != 0) {
		ESP_LOGE(TAG, "conn %u: response framing failed (%u B)", conn_handle,
			 (unsigned)resp_len);
		return;
	}

	int rc = aliro_ble_central_send(conn_handle, frame, frame_len);

	ESP_LOGI(TAG, "conn %u: -> %u B response, phase %s (send rc=%d)", conn_handle,
		 (unsigned)resp_len, phase_str(s_dev.phase), rc);

	if (s_dev.phase == ALIRO_DEV_ESTABLISHED) {
		ESP_LOGI(TAG, "=== ESTABLISHED: URSK agreed with the reader ===");
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, s_dev.ursk, sizeof(s_dev.ursk), ESP_LOG_INFO);
		ESP_LOGI(TAG, "Stage 1a ends here: ranging on this URSK is Stage 1b.");
	}
}

static void on_closed(uint16_t conn_handle)
{
	ESP_LOGW(TAG, "transport closed (conn %u, phase %s)", conn_handle,
		 s_armed ? phase_str(s_dev.phase) : "unarmed");
	/* The URSK and both channel keys die with the connection; zero them rather
	 * than leave a live session key in RAM for the next walk-up to trip over. */
	if (conn_handle == s_conn) {
		memset(&s_dev, 0, sizeof(s_dev));
		s_armed = false;
	}
}

/**
 * Initialize the Aliro BLE central stack, register event callbacks for ready/data/closed, bring up
 * the PSA crypto backend, and begin scanning for an Aliro reader. Run forever, yielding
 * periodically.
 */
void app_main(void)
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
		ESP_LOGE(TAG, "PSA crypto init failed");
		return;
	}

	if (aliro_ble_central_start(&cfg) != 0) {
		ESP_LOGE(TAG, "BLE central start failed");
		return;
	}
	ESP_LOGI(TAG, "Aliro initiator up; scanning for a reader");

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
