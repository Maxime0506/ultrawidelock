// Device-side UWB ranging-setup driver: opens the reader's BleSK-sealed SDUs and
// answers them, walking AP-Completed -> Initiate-Ranging-Session -> M1 -> M2 ->
// M3 -> M4 until the reader starts its responder.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See ranging.h for scope. Summary: this ends at the end of ranging setup.
 */
#include "ranging.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aliro_ble_central.h"
#include "aliro_device.h"
#include "aliro_device_uwb.h"
#include "aliro_uwb_msg.h"
#include "aliro_uwb_msg_spec.h"
#include "prepoll_tx.h"

/* aliro_uwb_msg_parser.c does LOG_MODULE_DECLARE(woz_aliro_uwb) and the module's
 * own registration lives in aliro_uwb_adapter.c, which is reader-side and not
 * linked here. Someone in this image has to own the symbol, so it is this file.
 * If the adapter is ever linked into the initiator as well, this becomes a
 * duplicate registration and the linker says so, which is the failure we want. */
LOG_MODULE_REGISTER(woz_aliro_uwb, LOG_LEVEL_INF);

/* The largest post-auth SDU either side sends. M1 is the big one: a config-id
 * list, a pulse-shape-combo list, a channel bitmask and a session id, all TLV.
 * The reader's own inbound path uses 256 for the same traffic. */
#define RANGING_SDU_MAX 256u

static struct aliro_device *s_dev;
static uint16_t s_conn;
static bool s_active;

/* Carried from M1 and M2 to the Pre-POLL, which needs values from three
 * different messages. The session id is the reader's (M1); the RAN multiplier
 * and channel are this board's own selections (M2), and the reader is bound to
 * them by the time it answers M3. */
static uint32_t s_session_id;
static uint8_t s_ran_multiplier;
static uint8_t s_channel;

/* Where in the setup we are, for logging and for refusing out-of-order messages.
 * The device drives this: it asks for the session, then answers M1 and M3. */
enum ranging_step {
	RANGING_IDLE = 0,  /* ESTABLISHED, AP-Completed not seen yet */
	RANGING_REQUESTED, /* Initiate-Ranging-Session sent, expecting M1 */
	RANGING_M2_SENT,   /* M2 sent, expecting M3 */
	RANGING_M4_SENT,   /* M4 sent; the reader should now be ranging */
};
static enum ranging_step s_step;

static const char *step_str(enum ranging_step s)
{
	switch (s) {
	case RANGING_IDLE:
		return "IDLE";
	case RANGING_REQUESTED:
		return "REQUESTED";
	case RANGING_M2_SENT:
		return "M2_SENT";
	default:
		return "M4_SENT";
	}
}

/**
 * Seal one plaintext ranging SDU on the BleSK channel and put it on the wire.
 *
 * There is no outer envelope. A sealed SDU is already [proto][id][len_be16][ct||tag],
 * the same four-byte shape aliro_ble_frame produces, so framing it again would
 * give the reader a header describing a header.
 */
static int send_plain(const uint8_t *plain, size_t plain_len)
{
	uint8_t wire[RANGING_SDU_MAX];
	size_t wire_len = 0;
	int rc;

	if (aliro_dev_ble_seal(&s_dev->sc_ble, plain, plain_len, wire, sizeof(wire), &wire_len) !=
	    0) {
		LOG_ERR("BleSK seal failed (%u B plaintext)", (unsigned)plain_len);
		return -1;
	}
	rc = aliro_ble_central_send(s_conn, wire, wire_len);
	if (rc != 0) {
		LOG_ERR("send failed (%u B, rc=%d)", (unsigned)wire_len, rc);
		return -1;
	}
	return 0;
}

/** Send a built M2/M4 and free it, whatever the outcome. */
static int send_message(struct aliro_uwb_message *msg, const char *what)
{
	int rc;

	if (!msg) {
		LOG_ERR("%s build failed", what);
		return -1;
	}
	rc = send_plain(msg->data, msg->len);
	aliro_uwb_msg_free(msg);
	if (rc == 0) {
		LOG_INF("-> %s sent", what);
	}
	return rc;
}

/**
 * Ask the reader to begin ranging setup.
 *
 * Notification / Ranging / Initiate-Ranging, a zero-length attribute. The reader
 * routes this to aliro_uwb_session_init_setup(), which is the ONLY thing that
 * makes it emit M1 (aliro_uwb_msg.c, parse_ranging_notification -> the
 * INIT_RANGING case, and aliro_uwb_session.c's init_setup). Nothing else starts
 * the exchange, exactly as Initiate-Access-Protocol is what starts AUTH0.
 */
static int send_initiate_ranging(void)
{
	static const uint8_t k_initiate_ranging[] = {
		ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
		ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING,
		0x00,
		0x02, /* payload length */
		ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING,
		0x00, /* attribute length: the reader reads the id and ignores the value */
	};

	if (send_plain(k_initiate_ranging, sizeof(k_initiate_ranging)) != 0) {
		return -1;
	}
	LOG_INF("-> Initiate-Ranging-Session sent, expecting M1");
	s_step = RANGING_REQUESTED;
	return 0;
}

/** Answer M1 with M2: parse what the reader offered, pick from it, send. */
static void on_m1(const uint8_t *plain, size_t plain_len)
{
	struct aliro_dev_uwb_m1 m1;
	struct aliro_dev_uwb_m2_params p;

	if (aliro_dev_uwb_parse_m1(plain, plain_len, &m1) != 0) {
		LOG_ERR("M1 parse failed (%u B)", (unsigned)plain_len);
		LOG_HEXDUMP_ERR(plain, plain_len, "m1");
		return;
	}
	LOG_INF("M1: session 0x%08x, %u config(s), %u combo(s), channels 0x%02x",
		(unsigned)m1.session_id, (unsigned)m1.config_count, (unsigned)m1.combo_count,
		m1.channel_bitmask);

	aliro_dev_uwb_select_m2(&m1, &p);
	LOG_INF("M2: config 0x%04x, combo 0x%02x, channel 0x%02x, ran_mult %u",
		(unsigned)p.config_id, p.pulse_shape_combo, p.channel_bitmask, p.ran_multiplier);

	s_session_id = m1.session_id;
	s_ran_multiplier = p.ran_multiplier;
	/* M2 names exactly one channel, so the bitmask has exactly one bit and this
	 * is a decode, not a preference. */
	s_channel = (p.channel_bitmask & ALIRO_CHANNEL_BITMASK_CH9) ? 9u : 5u;

	if (send_message(aliro_dev_uwb_build_m2(&p), "M2") == 0) {
		s_step = RANGING_M2_SENT;
	}
}

/**
 * Answer M3 with M4.
 *
 * M4 carries the values the DEVICE picks for the round: the initial STS index,
 * the UWB clock reference, the hop-mode key and one sync code.
 *
 * Three of the four are now load-bearing. STS_Index0 feeds the UAD derivation
 * that produces the Pre-POLL's SrcLongAddr, so it is an input to the MIC both
 * sides compute; the sync code is the preamble code the frame is sent on; the
 * hop-mode key keys the round schedule. They stay literals only because the
 * device is free to choose them -- any value works so long as both ends use the
 * same one, which M4 is what guarantees.
 *
 * uwb_time0 is the exception and is still a meaningless 0. It is a DW3000
 * timestamp the reader would schedule its first RX slot against, and this board
 * is not yet keeping a block clock to give it one. That costs nothing today,
 * because the reader's Pre-POLL listener is a continuous self-rearming RX
 * rather than a scheduled window. It starts to matter at the POLL.
 *
 * These are also the constants tests/host/test_aliro_device_uwb.c:124-134
 * drives the real reader session with, where they take it to state RANGING.
 */
static void on_m3(const uint8_t *plain, size_t plain_len)
{
	struct aliro_dev_uwb_m3 m3;
	struct aliro_dev_uwb_m4_params p = {
		.sts_index0 = 0x1000u,
		.uwb_time0 = 0u,
		.hop_mode_key = 0x11223344u,
		.sync_code_index = 9u,
	};

	if (aliro_dev_uwb_parse_m3(plain, plain_len, &m3) != 0) {
		LOG_ERR("M3 parse failed (%u B)", (unsigned)plain_len);
		LOG_HEXDUMP_ERR(plain, plain_len, "m3");
		return;
	}
	LOG_INF("M3: ran_mult %u, chaps/slot %u, responders %u, slots/round %u", m3.ran_multiplier,
		m3.chaps_per_slot, m3.num_responders, m3.slots_per_round);
	LOG_INF("M3: sync mask 0x%08x, hop cfg 0x%02x, mac mode %u",
		(unsigned)m3.sync_code_index_bitmask, m3.hopping_config_bitmask, m3.mac_mode);

	if (send_message(aliro_dev_uwb_build_m4(&p), "M4") == 0) {
		struct prepoll_tx_params tx = {
			.ursk = s_dev->ursk,
			.uwb_session_id = s_session_id,
			.sts_index0 = p.sts_index0,
			.hop_key_rw = p.hop_mode_key,
			.n_ran_s = s_ran_multiplier,
			.n_chap_per_slot = m3.chaps_per_slot,
			.n_responder = m3.num_responders,
			.n_slot_per_round = m3.slots_per_round,
			.channel = s_channel,
			.sync_code_index = p.sync_code_index,
		};
		int rc;

		s_step = RANGING_M4_SENT;
		LOG_INF("=== ranging setup complete: the reader should now be a responder ===");

		/* Start transmitting only after M4 is on the wire. The reader arms its
		 * Pre-POLL listener out of M4; a frame sent before that is sent into a
		 * radio that is not listening, and the silence would read as a key
		 * failure. */
		rc = initiator_prepoll_tx_start(&tx);
		if (rc != 0) {
			LOG_ERR("Pre-POLL TX did not start (%d); setup is done but nothing "
				"will be on the air",
				rc);
		}
	}
}

void initiator_ranging_begin(struct aliro_device *dev, uint16_t conn)
{
	s_dev = dev;
	s_conn = conn;
	s_step = RANGING_IDLE;
	s_active = true;
	LOG_INF("ranging setup armed; waiting for Reader-Status AP-Completed");
}

void initiator_ranging_end(uint16_t conn)
{
	if (s_active && conn == s_conn) {
		s_active = false;
		/* The Pre-POLL is keyed by s_dev->ursk, which main.c zeroes when the
		 * connection drops. Stop the transmitter before that pointer goes
		 * stale rather than after. */
		initiator_prepoll_tx_stop();
		s_dev = NULL;
		s_step = RANGING_IDLE;
	}
}

int initiator_ranging_on_sdu(uint16_t conn, const uint8_t *wire, size_t wire_len)
{
	uint8_t plain[RANGING_SDU_MAX];
	size_t plain_len = 0;
	uint8_t proto, id;

	if (!s_active || conn != s_conn || !s_dev) {
		return -1;
	}

	/* A tag mismatch here is not noise. Both ends derive the BleSK from the
	 * reader's published version list, so the usual cause is a salt that does
	 * not match the peer's GATT read, and the whole ranging channel is dead
	 * from this point on -- worth an error rather than a debug line. */
	if (aliro_dev_ble_open(&s_dev->sc_ble, wire, wire_len, plain, sizeof(plain), &plain_len) !=
	    0) {
		LOG_ERR("BleSK open failed (%u B) in step %s", (unsigned)wire_len,
			step_str(s_step));
		LOG_HEXDUMP_ERR(wire, wire_len, "sealed sdu");
		return -1;
	}

	proto = plain[0];
	id = plain[1];

	switch (proto) {
	case ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION:
		if (id == ALIRO_UWB_MESSAGE_NOTIFICATION_READER_STATUS_AP) {
			/* Reader-Status Access-Protocol-Completed. The reader will not
			 * range until the device asks, and the device must not ask
			 * before this arrives. */
			LOG_INF("Reader-Status: AP completed");
			if (s_step == RANGING_IDLE) {
				send_initiate_ranging();
			}
		} else {
			LOG_INF("notification id 0x%02x (%u B), not acted on", id,
				(unsigned)plain_len);
			LOG_HEXDUMP_INF(plain, plain_len, "notification");
		}
		return 0;

	case ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE:
		switch (id) {
		case ALIRO_UWB_MESSAGE_SETUP_M1:
			on_m1(plain, plain_len);
			return 0;
		case ALIRO_UWB_MESSAGE_SETUP_M3:
			on_m3(plain, plain_len);
			return 0;
		default:
			LOG_WRN("ranging-service id 0x%02x unhandled in step %s", id,
				step_str(s_step));
			LOG_HEXDUMP_WRN(plain, plain_len, "ranging sdu");
			return 0;
		}

	default:
		LOG_INF("post-auth SDU proto 0x%02x id 0x%02x (%u B)", proto, id,
			(unsigned)plain_len);
		LOG_HEXDUMP_INF(plain, plain_len, "sdu");
		return 0;
	}
}
