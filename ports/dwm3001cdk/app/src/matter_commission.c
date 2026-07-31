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

#include "aliro_hash.h" /* aliro_sha256, for the CASE transcript */
#include "aliro_prim.h" /* aliro_random, the CSPRNG the reader already uses */
#include "matter_ble_zephyr.h"
#include "matter_attest.h"
#include "matter_case.h"
#include "matter_clusters.h"
#include "matter_commission.h"
#include "matter_exchange.h"
#include "matter_im.h"
#include "matter_msg.h"
#include "matter_pase_sm.h"
#include "matter_thread.h"

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
 * Sized by the Interaction Model rather than PASE now, and by attestation
 * rather than by a read: an AttestationResponse carries a 539-byte
 * certification declaration plus a signature, where the largest PASE message is
 * 128 bytes. Encryption adds MATTER_TAG_LEN on top of the cleartext.
 */
#define MATTER_REPORT_MAX 1024u
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

/*
 * The two seams matter_attest.h declares. Kept here rather than in the module
 * so woz_matter stays free of any particular crypto backend; on this board both
 * are the reader's existing PSA-backed primitives.
 */
int matter_attest_ecdsa_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len,
			     uint8_t sig[MATTER_ATTEST_SIG_LEN])
{
	return aliro_ecdsa_p256_sign(priv, msg, msg_len, sig);
}

int matter_attest_ec_keygen(uint8_t priv[32], uint8_t pub[65])
{
	return aliro_ec_p256_keygen(priv, pub);
}

/*
 * The two matter_case.h declares. ECDH yields the X coordinate only, which is
 * what the spec means by the shared secret -- the Y coordinate carries no
 * additional entropy and including it would give a secret neither peer agrees
 * on.
 */
int matter_case_ecdh(const uint8_t priv[32], const uint8_t peer_pub[MATTER_CASE_PUBKEY_LEN],
		     uint8_t secret_out[MATTER_CASE_SECRET_LEN])
{
	return aliro_ecdh_p256(priv, peer_pub, secret_out);
}

int matter_case_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len,
		     uint8_t sig[MATTER_CASE_SIG_LEN])
{
	return aliro_ecdsa_p256_sign(priv, msg, msg_len, sig);
}

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

	/*
	 * A new commissioner is starting, so whatever the last one left behind
	 * is now stale. Doing it here rather than on link loss is what lets a
	 * commissioner close BLE and carry on over Thread with its fabric
	 * intact, while a genuine retry still gets a clean table.
	 */
	if (s_info.fabric.index != 0u && !s_info.commissioning_complete) {
		LOG_INF("new commissioner; rolling back fabric %u", s_info.fabric.index);
	}
	matter_clusters_failsafe_expire(&s_info);

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

		LOG_DBG("  read[%u] endpoint %d cluster 0x%04x attribute 0x%04x", i,
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
	LOG_DBG("ReportData: %u paths asked, %u B report, %u B sealed, rc=%d", s_read.n_paths,
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
	LOG_DBG("invoke: endpoint %u cluster 0x%04x command 0x%04x, %u B fields", inv.endpoint,
		(unsigned int)inv.cluster, (unsigned int)inv.command, (unsigned int)inv.fields_len);

	rc = matter_im_invoke_response_encode(&s_im, &inv, s_report, sizeof(s_report), &resp_len);
	if (rc != MATTER_OK) {
		LOG_ERR("cannot build InvokeResponse (%d)", rc);
		return;
	}
	if (inv.cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING &&
	    inv.command == MATTER_CMD_NC_ADD_OR_UPDATE_THREAD_NETWORK) {
		/*
		 * Length and shape only. The full dataset was dumped while
		 * there was no Thread stack to hand it to and the trace was the
		 * only way to see what arrived; now OpenThread consumes it, and
		 * a hexdump of the network key would be 700 bytes of an 8 KB
		 * trace buffer spent on printing a secret.
		 */
		LOG_INF("  Thread dataset: %u B, extended PAN id %s",
			(unsigned int)s_info.thread_dataset_len,
			s_info.have_thread_xpanid ? "found" : "MISSING");
	}
	if (inv.cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS &&
	    inv.command == MATTER_CMD_OC_ADD_NOC) {
		/*
		 * The verdict is inside the response payload, so without this
		 * an AddNOC this node REFUSED looks identical in the log to one
		 * it accepted. Split into halves because the log backend
		 * formats 32 bits at a time. Neither id is a secret: both are
		 * published in the clear once this node advertises operationally.
		 */
		LOG_INF("  AddNOC -> status %u, fabric index %u, node %08x%08x on fabric %08x%08x",
			s_info.last_noc_status, s_info.fabric.index,
			(unsigned int)(s_info.fabric.node_id >> 32),
			(unsigned int)s_info.fabric.node_id,
			(unsigned int)(s_info.fabric.fabric_id >> 32),
			(unsigned int)s_info.fabric.fabric_id);
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
	LOG_DBG("InvokeResponse: %u B, %u B sealed, rc=%d", (unsigned int)resp_len,
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

/**
 * State this node keeps between Sigma1 and Sigma3.
 *
 * One CASE handshake at a time, because there is one commissioner and one
 * fabric. A second Sigma1 arriving mid-handshake overwrites this, which is the
 * correct thing rather than a limitation: a commissioner that resent Sigma1 has
 * abandoned the earlier attempt, and its ephemeral key with it.
 */
static struct {
	uint8_t shared[MATTER_CASE_SECRET_LEN];
	uint8_t eph_priv[32];
	uint8_t eph_pub[MATTER_CASE_PUBKEY_LEN];
	uint8_t responder_random[MATTER_CASE_RANDOM_LEN];
	uint8_t resumption_id[16];
	uint16_t peer_session_id;
	uint16_t local_session_id;
	bool active;
} s_case;

/** The unencrypted Matter message counter for the operational exchange. */
static uint32_t s_case_counter;

/**
 * Build and frame the Sigma2 answering @p s1.
 *
 * @param sigma1 the Sigma1 payload EXACTLY as it arrived -- the transcript hash
 *        is over those bytes, and rebuilding them would be rebuilding something
 *        the peer hashed and this node did not.
 */
static size_t send_sigma2(const struct matter_case_sigma1 *s1, const uint8_t *ipk,
			  const uint8_t *sigma1, size_t sigma1_len,
			  const struct matter_proto_header *req, uint32_t req_counter,
			  uint8_t *reply, size_t cap)
{
	struct matter_case_sigma2_in in;
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	uint8_t transcript[32];
	size_t s2_len = 0u;
	size_t mh_len = 0u;
	size_t ph_len = 0u;
	int rc;

	if (aliro_ec_p256_keygen(s_case.eph_priv, s_case.eph_pub) != 0 ||
	    aliro_random(s_case.responder_random, sizeof(s_case.responder_random)) != 0 ||
	    aliro_random(s_case.resumption_id, sizeof(s_case.resumption_id)) != 0 ||
	    aliro_random((uint8_t *)&s_case.local_session_id, sizeof(s_case.local_session_id)) !=
		    0) {
		LOG_ERR("  no entropy for Sigma2");
		return 0u;
	}
	/* Session id 0 means "unsecured" on the wire, so it can never be ours. */
	if (s_case.local_session_id == 0u) {
		s_case.local_session_id = 1u;
	}
	s_case.peer_session_id = s1->initiator_session_id;

	aliro_sha256(sigma1, sigma1_len, transcript);

	memset(&in, 0, sizeof(in));
	in.initiator_pubkey = s1->initiator_pubkey;
	in.transcript_hash = transcript;
	in.ipk = ipk;
	in.noc = s_info.fabric.noc;
	in.noc_len = s_info.fabric.noc_len;
	in.icac = s_info.fabric.icac_len > 0u ? s_info.fabric.icac : NULL;
	in.icac_len = s_info.fabric.icac_len;
	in.op_priv = s_info.op_priv;
	in.responder_random = s_case.responder_random;
	in.responder_eph_priv = s_case.eph_priv;
	in.responder_eph_pub = s_case.eph_pub;
	in.resumption_id = s_case.resumption_id;
	in.responder_session_id = s_case.local_session_id;

	/* Framed after both headers, so the payload lands where it will be sent
	 * from rather than being copied into place afterwards. */
	/*
	 * The S flag and a source node id, because Apple sets them on its
	 * Sigma1 and a receiver correlates an unauthenticated exchange by who
	 * sent it. A Sigma2 that names nobody has nothing for the peer to match
	 * against its pending session, and gets dropped without a word --
	 * exactly what was observed.
	 */
	mh.flags = MATTER_MSG_FLAG_S;
	mh.session_id = 0u;
	mh.security_flags = 0u;
	/*
	 * Randomised once, not started at zero. The spec requires the global
	 * unencrypted counter to begin at a random value; Apple's is around
	 * 118 million, and a peer that saw this node restart at 1 would have
	 * every reason to treat the message as a replay of an old one.
	 */
	if (s_case_counter == 0u) {
		if (aliro_random((uint8_t *)&s_case_counter, sizeof(s_case_counter)) != 0) {
			LOG_ERR("  no entropy for the message counter");
			return 0u;
		}
		s_case_counter &= 0x0FFFFFFFu; /* leave room to increment */
	}
	mh.message_counter = ++s_case_counter;
	mh.source_node_id = s_info.fabric.node_id;
	mh.dest_node_id = 0u;
	mh.dest_group_id = 0u;
	rc = matter_msg_header_encode(&mh, reply, cap, &mh_len);
	if (rc != MATTER_OK) {
		LOG_ERR("  cannot frame Sigma2 (%d)", rc);
		return 0u;
	}

	/*
	 * The responder is NOT the exchange initiator, so the I flag stays
	 * clear; it acknowledges the Sigma1 it is answering, and asks to be
	 * acknowledged in turn.
	 */
	ph.exchange_flags = MATTER_EX_FLAG_A | MATTER_EX_FLAG_R;
	ph.opcode = MATTER_OP_CASE_SIGMA2;
	ph.exchange_id = req->exchange_id;
	ph.vendor_id = 0u;
	ph.protocol_id = MATTER_PROTOCOL_SECURE_CHANNEL;
	/* Acknowledging the Sigma1's counter, not a fresh one -- an ack that
	 * names the wrong message is a retransmission trigger, not an ack. */
	ph.ack_counter = req_counter;
	rc = matter_proto_header_encode(&ph, reply + mh_len, cap - mh_len, &ph_len);
	if (rc != MATTER_OK) {
		LOG_ERR("  cannot frame Sigma2 protocol header (%d)", rc);
		return 0u;
	}

	rc = matter_case_sigma2_encode(&in, reply + mh_len + ph_len, cap - mh_len - ph_len, &s2_len,
				       s_case.shared);
	memset(transcript, 0, sizeof(transcript));
	if (rc != MATTER_OK) {
		LOG_ERR("  Sigma2 could not be built (%d)", rc);
		return 0u;
	}

	s_case.active = true;
	LOG_INF("  Sigma2 out: %u B payload, %u B total, session 0x%04x", (unsigned int)s2_len,
		(unsigned int)(mh_len + ph_len + s2_len), (unsigned int)s_case.local_session_id);
	return mh_len + ph_len + s2_len;
}

/**
 * A datagram on the operational port. Sigma1, so far, and only Sigma1.
 *
 * There is no responder yet, so this answers nothing. What it does establish is
 * the thing that cannot be checked any other way: whether the identity the
 * initiator is asking for is THIS node's. The destination identifier is an HMAC
 * under the fabric's operational IPK, so recomputing it and finding a match
 * proves the whole chain -- AddNOC's IPK, the compressed fabric id derived from
 * the root key, the fabric and node ids out of the NOC -- all agree with what a
 * real commissioner computed independently.
 */
size_t matter_thread_on_datagram(const uint8_t *msg, size_t len, uint8_t *reply, size_t cap)
{
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	struct matter_case_sigma1 s1;
	uint8_t cfid[MATTER_COMPRESSED_FABRIC_LEN];
	uint8_t ipk[MATTER_CASE_IPK_LEN];
	uint8_t want[MATTER_CASE_DEST_ID_LEN];
	size_t mh_len = 0u;
	size_t ph_len = 0u;
	int rc;

	rc = matter_msg_header_decode(msg, len, &mh, &mh_len);
	if (rc != MATTER_OK) {
		LOG_WRN("  not a Matter message (%d)", rc);
		return 0u;
	}
	rc = matter_proto_header_decode(msg + mh_len, len - mh_len, &ph, &ph_len);
	if (rc != MATTER_OK) {
		LOG_WRN("  no protocol header (%d)", rc);
		return 0u;
	}
	/*
	 * The inbound header, so the reply can be compared against it. A Sigma2
	 * that is rejected without a word back looks identical whether the TLV
	 * is wrong or the framing is, and only one of those is visible here.
	 */
	LOG_INF("  protocol 0x%04x opcode 0x%02x exchange 0x%04x", (unsigned int)ph.protocol_id,
		ph.opcode, (unsigned int)ph.exchange_id);
	LOG_INF("  in hdr: flags 0x%02x sec 0x%02x ctr %u exflags 0x%02x", mh.flags,
		mh.security_flags, (unsigned int)mh.message_counter, ph.exchange_flags);

	if (ph.protocol_id != MATTER_PROTOCOL_SECURE_CHANNEL ||
	    ph.opcode != MATTER_OP_CASE_SIGMA1) {
		return 0u;
	}

	rc = matter_case_sigma1_decode(msg + mh_len + ph_len, len - mh_len - ph_len, &s1);
	if (rc != MATTER_OK) {
		LOG_WRN("  Sigma1 unreadable (%d)", rc);
		return 0u;
	}
	LOG_INF("  Sigma1: initiator session 0x%04x, resumption %s",
		(unsigned int)s1.initiator_session_id, s1.has_resumption ? "offered" : "none");

	if (s_info.fabric.index == 0u) {
		LOG_WRN("  no fabric to match it against");
		return 0u;
	}
	if (matter_fabric_compressed_id(s_info.fabric.root_public_key, s_info.fabric.fabric_id,
					cfid) != MATTER_OK ||
	    matter_case_operational_ipk(s_info.fabric.ipk, cfid, ipk) != MATTER_OK ||
	    matter_case_destination_id(ipk, s1.initiator_random, s_info.fabric.root_public_key,
				       s_info.fabric.fabric_id, s_info.fabric.node_id,
				       want) != MATTER_OK) {
		LOG_ERR("  could not recompute the destination identifier");
		return 0u;
	}

	if (memcmp(want, s1.destination_id, sizeof(want)) != 0) {
		LOG_WRN("  destination does NOT match fabric %u", s_info.fabric.index);
		return 0u;
	}
	LOG_INF("  destination MATCHES fabric %u -- answering", s_info.fabric.index);

	return send_sigma2(&s1, ipk, msg + mh_len + ph_len, len - mh_len - ph_len, &ph,
			   mh.message_counter, reply, cap);
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
		/* Every attestation signature covers this. Copied now because
		 * the PASE responder is wiped when the next session starts. */
		memcpy(s_info.attestation_challenge, s_pase.keys.attestation_challenge,
		       sizeof(s_info.attestation_challenge));
		s_info.have_challenge = true;

		LOG_INF("PASE complete: secure session up (local 0x%04x, peer 0x%04x)",
			(unsigned int)s_pase.local_session_id,
			(unsigned int)s_pase.peer_session_id);
	}
}

/** The link dropped. Cheap here; begin_session() does the real work later. */
static void on_link_reset(void)
{
	s_stale = true;

	/*
	 * NOT the place to roll the fail-safe back, which is what this used to
	 * do. A commissioner with concurrent connection closes BLE ON PURPOSE
	 * once ConnectNetwork succeeds and finishes over Thread -- so the link
	 * dropping is the normal path, and discarding the fabric here destroyed
	 * the identity CASE was about to use. Observed exactly that: two Sigma1s
	 * matched, then "no fabric to match it against" for every one after.
	 *
	 * The rollback belongs at the start of the NEXT commissioning attempt
	 * instead; see begin_session().
	 */
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
