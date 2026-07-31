/**
 * @file matter_commission.c — joins BTP, the exchange and PASE.
 *
 * Three finished pieces and no protocol of its own:
 *
 *   matter_ble_zephyr.c   bytes in and out over the 0xFFF6 service
 *   matter_exchange.c     which session, which exchange, duplicate, ack
 *   matter_pase_sm.c      the five commissioning messages
 *
 * What is left for this file is the wiring nobody else can do: pulling the
 * SPAKE2+ verifier out of configuration, drawing real randomness, and deciding
 * what happens when a commissioner disappears halfway through.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 3 of internal/cdk-matter-plan.md, the part that makes the rest reachable.
 *
 * The device holds a verifier, never the passcode -- see matter_pase_sm.h. The
 * default in Kconfig is CHIP's own published test verifier for passcode
 * 20202021, reproduced byte for byte by scripts/spake2p_verifier.py and checked
 * against workspace/modules/lib/matter/src/include/platform/
 * TestOnlyCommissionableDataProvider.h:74-79. A real deployment runs that script
 * with its own passcode.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "aliro_prim.h" /* aliro_random, the CSPRNG the reader already uses */
#include "matter_ble_zephyr.h"
#include "matter_clusters.h"
#include "matter_commission.h"
#include "matter_exchange.h"
#include "matter_im.h"
#include "matter_pase_sm.h"

LOG_MODULE_DECLARE(matter_ble, CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL);

static struct matter_exchange s_exchange;
static struct matter_pase_responder s_pase;
static struct matter_pase_verifier s_verifier;
static bool s_verifier_ok;

/**
 * Set when the link dropped, cleared when the next message re-seeds.
 *
 * The reset is deferred rather than done in the Bluetooth callback because it
 * needs fresh randomness, and drawing entropy from a connection callback to
 * serve a session that may never arrive is work for nothing.
 */
static bool s_stale = true;

/**
 * What this node says it is, and the data model built over it.
 *
 * Sized in seconds and enum values rather than Kconfig strings because these
 * are what the commissioner reads back; see matter_clusters.h for each.
 */
static struct matter_device_info s_info = {
	.vendor_id = CONFIG_ALIRO_MATTER_VENDOR_ID,
	.product_id = CONFIG_ALIRO_MATTER_PRODUCT_ID,
	.breadcrumb = 0u,
	.regulatory_config = MATTER_REGULATORY_INDOOR,
	.location_capability = MATTER_REGULATORY_INDOOR,
	/* The fail-safe window a commissioner may arm, and the ceiling it may
	 * extend to. CHIP's own defaults; nothing here is slow enough to need
	 * more. */
	.failsafe_expiry_s = 60u,
	.failsafe_max_s = 900u,
	/*
	 * True keeps BLE up across the whole of commissioning. False would tell
	 * the commissioner to expect this node to leave BLE and reappear on its
	 * operational network -- which it cannot do, having no Thread or Wi-Fi
	 * yet, so false would promise a return that never happens.
	 */
	.supports_concurrent_connection = true,
};
static struct matter_im_server s_im;

/**
 * Framed reply: both headers, the largest message, and the AEAD tag.
 *
 * Sized by the Interaction Model rather than PASE now: a ReportData answering
 * a commissioner's opening read is ~200 bytes where the largest PASE message is
 * 128. Encryption adds MATTER_TAG_LEN on top of the cleartext.
 */
#define MATTER_REPORT_MAX 512u
static uint8_t s_out[MATTER_EXCHANGE_HEADER_MAX + MATTER_REPORT_MAX + MATTER_TAG_LEN];

/** The Interaction Model payload, before framing. */
static uint8_t s_report[MATTER_REPORT_MAX];

/**
 * Plaintext of an encrypted message.
 *
 * Separate from the BTP reassembly area rather than decrypted in place: the
 * ciphertext has to stay intact while its tag is checked, and aliasing the two
 * would make that depend on the cipher's write order.
 */
static uint8_t s_pt[CONFIG_ALIRO_MATTER_BLE_RX_BUF];

/** @return 0 and the byte count, or -EINVAL on any non-hex or odd-length input. */
static int unhex(const char *s, uint8_t *out, size_t cap, size_t *len)
{
	size_t n = strlen(s);
	size_t i;

	if ((n % 2u) != 0u || (n / 2u) > cap) {
		return -EINVAL;
	}
	for (i = 0; i < n; i += 2u) {
		unsigned int hi, lo;

		if (!isxdigit((int)s[i]) || !isxdigit((int)s[i + 1u])) {
			return -EINVAL;
		}
		hi = (unsigned int)((s[i] > '9') ? ((s[i] | 0x20) - 'a' + 10) : (s[i] - '0'));
		lo = (unsigned int)((s[i + 1u] > '9') ? ((s[i + 1u] | 0x20) - 'a' + 10)
						      : (s[i + 1u] - '0'));
		out[i / 2u] = (uint8_t)((hi << 4) | lo);
	}
	*len = n / 2u;
	return 0;
}

/**
 * Read the verifier out of Kconfig.
 *
 * A verifier and the parameters that produced it have to agree, and nothing on
 * this device can check that they do -- a mismatched pair fails at Pake3 with
 * cA wrong and no way to tell that apart from a wrong passcode. So this checks
 * the shapes it can and says so loudly when they are wrong, which is the only
 * warning anyone gets.
 */
static int load_verifier(void)
{
	uint8_t blob[MATTER_SPAKE_SCALAR_LEN + MATTER_SPAKE_POINT_LEN];
	size_t blob_len = 0u;
	size_t salt_len = 0u;

	if (unhex(CONFIG_ALIRO_MATTER_SPAKE2P_VERIFIER, blob, sizeof(blob), &blob_len) != 0) {
		LOG_ERR("SPAKE2P verifier is not %u bytes of hex", (unsigned int)sizeof(blob));
		return -EINVAL;
	}
	if (blob_len != sizeof(blob)) {
		LOG_ERR("SPAKE2P verifier is %u bytes, expected %u", (unsigned int)blob_len,
			(unsigned int)sizeof(blob));
		return -EINVAL;
	}
	/* L must be an uncompressed point. Cheap, and it catches a verifier
	 * pasted in the wrong order -- w0 and L swapped would otherwise only
	 * show up as an unexplainable commissioning failure. */
	if (blob[MATTER_SPAKE_SCALAR_LEN] != 0x04u) {
		LOG_ERR("SPAKE2P verifier: L does not start 0x04; w0 and L swapped?");
		return -EINVAL;
	}

	if (unhex(CONFIG_ALIRO_MATTER_SPAKE2P_SALT, s_verifier.salt, sizeof(s_verifier.salt),
		  &salt_len) != 0) {
		LOG_ERR("SPAKE2P salt is not valid hex, or longer than %u bytes",
			(unsigned int)sizeof(s_verifier.salt));
		return -EINVAL;
	}

	memcpy(s_verifier.w0, blob, MATTER_SPAKE_SCALAR_LEN);
	memcpy(s_verifier.l, blob + MATTER_SPAKE_SCALAR_LEN, MATTER_SPAKE_POINT_LEN);
	s_verifier.salt_len = (uint8_t)salt_len;
	s_verifier.iterations = CONFIG_ALIRO_MATTER_SPAKE2P_ITERATIONS;

	return 0;
}

/** Fresh randomness for one commissioning attempt. */
static int begin_session(void)
{
	uint8_t responder_random[MATTER_PASE_RANDOM_LEN];
	uint8_t y_entropy[MATTER_PASE_Y_ENTROPY_LEN];
	uint8_t seed[sizeof(uint32_t) + sizeof(uint16_t)];
	uint32_t counter_seed;
	uint16_t session_id;
	int rc;

	if (aliro_random(responder_random, sizeof(responder_random)) != 0 ||
	    aliro_random(y_entropy, sizeof(y_entropy)) != 0 ||
	    aliro_random(seed, sizeof(seed)) != 0) {
		LOG_ERR("CSPRNG failed; refusing to start PASE");
		return -EIO;
	}
	memcpy(&counter_seed, seed, sizeof(counter_seed));
	memcpy(&session_id, seed + sizeof(counter_seed), sizeof(session_id));
	/* Session id 0 is the unsecured session by definition, so it can never
	 * name the secure one PASE is about to create. */
	if (session_id == 0u) {
		session_id = 1u;
	}

	/* false: this exchange runs over BTP, which is already reliable, so MRP
	 * is off. Matter gates that on the transport -- SecureSession.h:161. */
	matter_exchange_init(&s_exchange, counter_seed, false);
	rc = matter_pase_responder_init(&s_pase, &s_verifier, session_id, responder_random,
					y_entropy);
	if (rc != MATTER_OK) {
		LOG_ERR("PASE responder init rc=%d (verifier parameters out of range?)", rc);
		return -EINVAL;
	}

	s_stale = false;
	return 0;
}

static void send_framed(uint8_t opcode, const uint8_t *payload, size_t len)
{
	size_t framed = 0u;
	int rc;

	rc = matter_exchange_reply(&s_exchange, opcode, payload, len, s_out, sizeof(s_out),
				   &framed);
	if (rc != MATTER_OK) {
		LOG_ERR("framing opcode 0x%02x rc=%d", opcode, rc);
		return;
	}
	LOG_HEXDUMP_DBG(s_out, framed > 40u ? 40u : framed, "reply (first bytes)");
	rc = matter_ble_send(s_out, framed);
	LOG_DBG("sent opcode 0x%02x, %u B framed, rc=%d", opcode, (unsigned int)framed, rc);
}

/**
 * Answer a ReadRequest.
 *
 * Kept static rather than on the stack: the request holds up to
 * MATTER_IM_MAX_PATHS paths and the report another half kilobyte, and this runs
 * on the Matter work queue, whose size is still an argument rather than a
 * measurement (see CONFIG_ALIRO_MATTER_BLE_WQ_STACK). Only one commissioner is
 * ever served at a time, so one of each is enough.
 */
static struct matter_im_read s_read;

static void on_read_request(const struct matter_exchange_in *in)
{
	struct matter_im_report_stats stats;
	size_t report_len = 0u;
	size_t framed = 0u;
	int rc;

	rc = matter_im_read_request_decode(in->payload, in->payload_len, &s_read);
	if (rc != MATTER_OK) {
		LOG_WRN("unreadable ReadRequest (%d), %u B", rc, (unsigned int)in->payload_len);
		return;
	}

	/* What was asked, not just how much. Which paths a commissioner reads is
	 * the specification for what to implement next, and reading it out of a
	 * hexdump by hand has already cost more than this line does. */
	for (uint8_t i = 0; i < s_read.n_paths; i++) {
		const struct matter_im_path *p = &s_read.paths[i];

		LOG_INF("  read[%u] endpoint %d cluster 0x%04x attribute 0x%04x", i,
			p->have_endpoint ? (int)p->endpoint : -1,
			p->have_cluster ? (unsigned int)p->cluster : 0xFFFFu,
			p->have_attribute ? (unsigned int)p->attribute : 0xFFFFu);
	}

	rc = matter_im_report_data_encode(&s_im, &s_read, s_report, sizeof(s_report), &report_len,
					  &stats);
	if (rc != MATTER_OK) {
		LOG_ERR("cannot build ReportData for %u paths (%d)", s_read.n_paths, rc);
		return;
	}
	if (stats.unexpanded_wildcard > 0u) {
		/* Loud on purpose: the answer went out short, and to the
		 * commissioner that is indistinguishable from an empty cluster. */
		LOG_WRN("%u wildcard path(s) not expanded; report is incomplete",
			stats.unexpanded_wildcard);
	}

	rc = matter_exchange_send(&s_exchange, MATTER_PROTOCOL_INTERACTION_MODEL,
				  MATTER_IM_OP_REPORT_DATA, s_report, report_len, s_out,
				  sizeof(s_out), &framed);
	if (rc != MATTER_OK) {
		LOG_ERR("framing ReportData rc=%d (%u B report)", rc, (unsigned int)report_len);
		return;
	}

	rc = matter_ble_send(s_out, framed);
	LOG_INF("ReportData: %u paths asked, %u B report, %u B sealed, rc=%d", s_read.n_paths,
		(unsigned int)report_len, (unsigned int)framed, rc);
}

static void on_invoke_request(const struct matter_exchange_in *in)
{
	static struct matter_im_invoke inv;
	size_t resp_len = 0u;
	size_t framed = 0u;
	int rc;

	rc = matter_im_invoke_request_decode(in->payload, in->payload_len, &inv);
	if (rc != MATTER_OK) {
		LOG_WRN("unreadable InvokeRequest (%d), %u B", rc, (unsigned int)in->payload_len);
		return;
	}
	LOG_INF("invoke: endpoint %u cluster 0x%04x command 0x%04x, %u B fields", inv.endpoint,
		(unsigned int)inv.cluster, (unsigned int)inv.command, (unsigned int)inv.fields_len);

	rc = matter_im_invoke_response_encode(&s_im, &inv, s_report, sizeof(s_report), &resp_len);
	if (rc != MATTER_OK) {
		LOG_ERR("cannot build InvokeResponse (%d)", rc);
		return;
	}
	if (resp_len == 0u) {
		/* The command ran; the peer asked not to be told. */
		LOG_INF("invoke done, response suppressed");
		return;
	}

	rc = matter_exchange_send(&s_exchange, MATTER_PROTOCOL_INTERACTION_MODEL,
				  MATTER_IM_OP_INVOKE_COMMAND_RESPONSE, s_report, resp_len, s_out,
				  sizeof(s_out), &framed);
	if (rc != MATTER_OK) {
		LOG_ERR("framing InvokeResponse rc=%d (%u B)", rc, (unsigned int)resp_len);
		return;
	}

	rc = matter_ble_send(s_out, framed);
	LOG_INF("InvokeResponse: %u B, %u B sealed, rc=%d", (unsigned int)resp_len,
		(unsigned int)framed, rc);
}

static void on_secure(const struct matter_exchange_in *in)
{
	if (in->protocol_id == MATTER_PROTOCOL_INTERACTION_MODEL &&
	    in->opcode == MATTER_IM_OP_READ_REQUEST) {
		on_read_request(in);
		return;
	}
	if (in->protocol_id == MATTER_PROTOCOL_INTERACTION_MODEL &&
	    in->opcode == MATTER_IM_OP_INVOKE_COMMAND_REQUEST) {
		on_invoke_request(in);
		return;
	}

	/*
	 * Anything else is the next piece of work, and the log line names it so
	 * that piece is aimed rather than guessed -- which is how the read above
	 * came to be written. The payload is dumped for the same reason: its
	 * length is never the useful part.
	 *
	 * Safe to log. These are commissioning messages over a session whose
	 * keys are gone by the time anyone reads the trace, and no PASE secret
	 * is reachable from here.
	 */
	LOG_INF("secure message: protocol 0x%04x opcode 0x%02x, %u B payload (unhandled)",
		(unsigned int)in->protocol_id, in->opcode, (unsigned int)in->payload_len);
	LOG_HEXDUMP_INF(in->payload, in->payload_len, "payload");
}

static void on_message(const uint8_t *msg, size_t len)
{
	struct matter_exchange_in in;
	uint8_t pase_out[MATTER_PASE_REPLY_MAX];
	size_t pase_len = 0u;
	uint8_t pase_op = 0u;
	int rc;

	if (!s_verifier_ok) {
		LOG_ERR("no usable SPAKE2P verifier; dropping %u bytes", (unsigned int)len);
		return;
	}
	if (s_stale && begin_session() != 0) {
		return;
	}

	LOG_DBG("on_message: %u B", (unsigned int)len);
	rc = matter_exchange_recv(&s_exchange, msg, len, &in, s_pt, sizeof(s_pt));
	LOG_DBG("exchange_recv rc=%d opcode=0x%02x payload=%u", rc, in.opcode,
		(unsigned int)in.payload_len);
	if (rc == MATTER_E_DUP) {
		/* The peer thinks its last message was lost. Acknowledge it
		 * again, but do NOT run the payload through PASE twice. */
		size_t framed = 0u;

		if (matter_exchange_standalone_ack(&s_exchange, s_out, sizeof(s_out), &framed) ==
		    MATTER_OK) {
			(void)matter_ble_send(s_out, framed);
		}
		return;
	}
	if (rc != MATTER_OK) {
		/* Name what was turned away. "refused 137 bytes (-4)" cost a whole
		 * hardware round to interpret; the exchange id and the I flag are
		 * what actually said which rule fired. */
		LOG_WRN("refused %u B (%d): protocol 0x%04x opcode 0x%02x exchange 0x%04x %s",
			(unsigned int)len, rc, (unsigned int)in.protocol_id, in.opcode,
			in.exchange_id, in.initiator ? "I" : "-");
		return;
	}

	if (s_exchange.secure) {
		on_secure(&in);
		return;
	}

	/* A bare acknowledgement closes out our last send and asks nothing. */
	if (in.opcode == MATTER_SC_OP_ACK) {
		return;
	}

	rc = matter_pase_responder_recv(&s_pase, in.opcode, in.payload, in.payload_len, pase_out,
					sizeof(pase_out), &pase_len, &pase_op);
	/* Send whatever came back BEFORE acting on rc: on a failure that reply is
	 * the StatusReport telling the peer why, and it is the only thing
	 * standing between a rejected commissioner and a silent timeout. */
	if (pase_len > 0u) {
		send_framed(pase_op, pase_out, pase_len);
	}

	if (rc != MATTER_OK) {
		LOG_WRN("PASE refused opcode 0x%02x (%d)", in.opcode, rc);
		/* Terminal for this attempt; the next connection re-seeds. */
		s_stale = true;
		return;
	}

	LOG_INF("PASE rc=%d, reply opcode 0x%02x (%u B), state=%d", rc, pase_op,
		(unsigned int)pase_len, (int)matter_pase_responder_state(&s_pase));

	if (matter_pase_responder_state(&s_pase) == MATTER_PASE_ST_DONE && !s_exchange.secure) {
		uint32_t seed = 0u;

		if (aliro_random((uint8_t *)&seed, sizeof(seed)) != 0) {
			LOG_ERR("CSPRNG failed; cannot open the secure session");
			s_stale = true;
			return;
		}
		/* The StatusReport went out on the unsecured session above; only
		 * now does the clear channel close. */
		rc = matter_exchange_promote(&s_exchange, s_pase.local_session_id,
					     s_pase.peer_session_id, &s_pase.keys, seed);
		if (rc != MATTER_OK) {
			LOG_ERR("promote to secure session rc=%d", rc);
			s_stale = true;
			return;
		}
		LOG_INF("PASE complete: secure session up (local 0x%04x, peer 0x%04x)",
			(unsigned int)s_pase.local_session_id,
			(unsigned int)s_pase.peer_session_id);
	}
}

/** The link dropped. Cheap here; begin_session() does the real work later. */
static void on_link_reset(void)
{
	s_stale = true;
}

int matter_commission_init(void)
{
	if (load_verifier() != 0) {
		/* Deliberately still registers the handler. A device that cannot
		 * commission should say so on every attempt rather than look
		 * like a dead radio. */
		s_verifier_ok = false;
	} else {
		s_verifier_ok = true;
		LOG_INF("commissioning ready: discriminator 0x%03x, %u PBKDF iterations",
			(unsigned int)CONFIG_ALIRO_MATTER_DISCRIMINATOR,
			(unsigned int)CONFIG_ALIRO_MATTER_SPAKE2P_ITERATIONS);
	}

	matter_clusters_init(&s_im, &s_info);
	matter_ble_set_link_handler(on_link_reset);
	matter_ble_set_msg_handler(on_message);
	return 0;
}
