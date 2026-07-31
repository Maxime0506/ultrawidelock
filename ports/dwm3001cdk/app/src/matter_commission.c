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

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
#include <mbedtls/memory_buffer_alloc.h>
#endif

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
/*
 * Sized by the whole data model in one message, not by any single attribute: a
 * controller subscribes to everything the moment it owns the node, and that
 * report measured 1079 B (tests/host/test_matter_im.c asserts it still fits).
 * A report that does not fit is not truncated, it is refused -- so undersizing
 * this produces a subscription that establishes and then never reports.
 */
#define MATTER_REPORT_MAX 1536u
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

int matter_case_verify(const uint8_t pub[MATTER_CASE_PUBKEY_LEN], const uint8_t *msg,
		       size_t msg_len, const uint8_t sig[MATTER_CASE_SIG_LEN])
{
	return aliro_ecdsa_p256_verify(pub, msg, msg_len, sig);
}

/*
 * NIST CAVP SP 800-56A ECC CDH P-256, COUNT 0.
 *
 * VERIFIED BEFORE USE, not trusted: both points were checked to satisfy
 * y^2 = x^3 - 3x + b, the private key to produce its own published public key
 * under scalar multiplication, and d*Q to reproduce Z. That check exists
 * because two vectors offered for this job turned out to have public keys that
 * were not on the curve at all -- and aliro_ecdh_p256() would have correctly
 * rejected them, which reads exactly like the bug being hunted.
 */
static const uint8_t k_kat_priv[] = {
	0x7D, 0x7D, 0xC5, 0xF7, 0x1E, 0xB2, 0x9D, 0xDA, 0xF8, 0x0D, 0x62,
	0x14, 0x63, 0x2E, 0xEA, 0xE0, 0x3D, 0x90, 0x58, 0xAF, 0x1F, 0xB6,
	0xD2, 0x2E, 0xD8, 0x0B, 0xAD, 0xB6, 0x2B, 0xC1, 0xA5, 0x34,
};
static const uint8_t k_kat_peer[] = {
	0x04, 0x70, 0x0C, 0x48, 0xF7, 0x7F, 0x56, 0x58, 0x4C, 0x5C, 0xC6, 0x32, 0xCA,
	0x65, 0x64, 0x0D, 0xB9, 0x1B, 0x6B, 0xAC, 0xCE, 0x3A, 0x4D, 0xF6, 0xB4, 0x2C,
	0xE7, 0xCC, 0x83, 0x88, 0x33, 0xD2, 0x87, 0xDB, 0x71, 0xE5, 0x09, 0xE3, 0xFD,
	0x9B, 0x06, 0x0D, 0xDB, 0x20, 0xBA, 0x5C, 0x51, 0xDC, 0xC5, 0x94, 0x8D, 0x46,
	0xFB, 0xF6, 0x40, 0xDF, 0xE0, 0x44, 0x17, 0x82, 0xCA, 0xB8, 0x5F, 0xA4, 0xAC,
};
static const uint8_t k_kat_z[] = {
	0x46, 0xFC, 0x62, 0x10, 0x64, 0x20, 0xFF, 0x01, 0x2E, 0x54, 0xA4,
	0x34, 0xFB, 0xDD, 0x2D, 0x25, 0xCC, 0xC5, 0x85, 0x20, 0x60, 0x56,
	0x1E, 0x68, 0x04, 0x0D, 0xD7, 0x77, 0x89, 0x97, 0xBD, 0x7B,
};
/**
 * Prove the ECDH primitive against a published answer, once, at boot.
 *
 * The shared secret is the only input to CASE that neither peer can check
 * alone: get it wrong and the other side simply cannot decrypt, with nothing
 * on the wire to say so. This is the one place it can be pinned to something
 * external.
 */
static void ecdh_known_answer_test(void)
{
	uint8_t z[32];

	if (aliro_ecdh_p256(k_kat_priv, k_kat_peer, z) != 0) {
		LOG_ERR("ECDH self-test: primitive REFUSED the NIST vector");
		return;
	}
	if (memcmp(z, k_kat_z, sizeof(z)) == 0) {
		LOG_INF("ECDH self-test: PASS (NIST CAVP P-256 CDH count 0)");
		return;
	}
	/* Byte-reversal is the classic failure of a hardware accelerator fed
	 * the wrong way round, and worth naming rather than leaving as "wrong". */
	{
		uint8_t rev[32];
		size_t i;

		for (i = 0u; i < sizeof(rev); i++) {
			rev[i] = z[sizeof(rev) - 1u - i];
		}
		LOG_ERR("ECDH self-test: FAIL%s", memcmp(rev, k_kat_z, sizeof(rev)) == 0
							  ? " -- output is BYTE-REVERSED"
							  : "");
	}
	LOG_HEXDUMP_ERR(z, sizeof(z), "got");
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
	if (s_info.fabrics[0].index != 0u && !s_info.commissioning_complete) {
		LOG_INF("new commissioner; rolling back every fabric");
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

/**
 * The operational session, once CASE has established one.
 *
 * Separate from s_exchange, which belongs to BLE: the two have different keys
 * and different counter spaces, and Apple keeps BLE open across the handover --
 * so both can be live at once and neither may borrow the other's counter.
 */
static struct matter_exchange s_case_x;
static bool s_case_ready;

/**
 * Where a reply goes when the request arrived over Thread rather than BLE.
 *
 * The two transports answer in opposite directions: matter_ble_send() pushes,
 * while the Thread port sends whatever matter_thread_on_datagram() RETURNS. So
 * a CASE reply has to travel back up the call stack instead of out, and the
 * handlers in between -- on_read_request, on_invoke_request -- have no business
 * knowing which of the two they are serving. Non-NULL means "stage it here".
 */
static uint8_t *s_thread_reply;
static size_t s_thread_reply_cap;
static size_t s_thread_reply_len;

static void send_framed(uint8_t opcode, const uint8_t *payload, size_t len)
{
	struct matter_exchange *x = (s_thread_reply != NULL) ? &s_case_x : &s_exchange;
	size_t framed = 0u;
	int rc;

	rc = matter_exchange_reply(x, opcode, payload, len, s_out, sizeof(s_out), &framed);
	if (rc != MATTER_OK) {
		LOG_ERR("framing opcode 0x%02x rc=%d", opcode, rc);
		return;
	}
	LOG_HEXDUMP_DBG(s_out, framed > 40u ? 40u : framed, "reply (first bytes)");

	if (s_thread_reply != NULL) {
		if (framed > s_thread_reply_cap) {
			LOG_ERR("opcode 0x%02x needs %u B, have %u", opcode, (unsigned int)framed,
				(unsigned int)s_thread_reply_cap);
			return;
		}
		memcpy(s_thread_reply, s_out, framed);
		s_thread_reply_len = framed;
		LOG_DBG("staged opcode 0x%02x, %u B for Thread", opcode, (unsigned int)framed);
		return;
	}

	rc = matter_ble_send(s_out, framed);
	LOG_DBG("sent opcode 0x%02x, %u B framed, rc=%d", opcode, (unsigned int)framed, rc);
}

/**
 * Send an Interaction Model message on whichever transport asked for it.
 *
 * The same split send_framed() makes, and for the same reason. Both IM paths
 * used to frame on the BLE exchange unconditionally, which over a CASE session
 * produced a perfectly correct response sealed with the wrong session's keys
 * and pushed at a link the commissioner had already closed -- no error
 * anywhere, and a commissioner left waiting for an answer that went out a
 * different door.
 */
static void send_im(uint8_t opcode, const uint8_t *payload, size_t len)
{
	struct matter_exchange *x = (s_thread_reply != NULL) ? &s_case_x : &s_exchange;
	size_t framed = 0u;
	int rc;

	rc = matter_exchange_send(x, MATTER_PROTOCOL_INTERACTION_MODEL, opcode, payload, len, s_out,
				  sizeof(s_out), &framed);
	if (rc != MATTER_OK) {
		LOG_ERR("framing IM opcode 0x%02x rc=%d (%u B)", opcode, rc, (unsigned int)len);
		return;
	}

	if (s_thread_reply != NULL) {
		if (framed > s_thread_reply_cap) {
			LOG_ERR("IM opcode 0x%02x needs %u B, have %u", opcode, (unsigned int)framed,
				(unsigned int)s_thread_reply_cap);
			return;
		}
		memcpy(s_thread_reply, s_out, framed);
		s_thread_reply_len = framed;
		LOG_DBG("  IM opcode 0x%02x staged over CASE, %u B", opcode, (unsigned int)framed);
		return;
	}

	rc = matter_ble_send(s_out, framed);
	LOG_DBG("IM opcode 0x%02x: %u B payload, %u B sealed, rc=%d", opcode, (unsigned int)len,
		(unsigned int)framed, rc);
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

		/*
		 * Loud only over CASE. The BLE phase is settled and its three
		 * reads carry nine paths each -- 27 lines that filled the trace
		 * ring before the interesting half of the session began.
		 */
		if (s_thread_reply == NULL) {
			LOG_DBG("  read[%u] endpoint %d cluster 0x%04x attribute 0x%04x", i,
				p->have_endpoint ? (int)p->endpoint : -1,
				p->have_cluster ? (unsigned int)p->cluster : 0xFFFFu,
				p->have_attribute ? (unsigned int)p->attribute : 0xFFFFu);
			continue;
		}
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

	send_im(MATTER_IM_OP_REPORT_DATA, s_report, report_len);
	LOG_DBG("ReportData: %u paths asked, %u B report", s_read.n_paths,
		(unsigned int)report_len);
}

static void on_invoke_request(const struct matter_exchange_in *in)
{
	static struct matter_im_invoke inv;
	size_t resp_len = 0u;
	int rc;

	rc = matter_im_invoke_request_decode(in->payload, in->payload_len, &inv);
	if (rc != MATTER_OK) {
		LOG_WRN("unreadable InvokeRequest (%d), %u B", rc, (unsigned int)in->payload_len);
		return;
	}
	if (s_thread_reply != NULL) {
		LOG_INF("  invoke: endpoint %u cluster 0x%04x command 0x%04x, %u B fields",
			inv.endpoint, (unsigned int)inv.cluster, (unsigned int)inv.command,
			(unsigned int)inv.fields_len);
	} else {
		LOG_DBG("  invoke: endpoint %u cluster 0x%04x command 0x%04x, %u B fields",
			inv.endpoint, (unsigned int)inv.cluster, (unsigned int)inv.command,
			(unsigned int)inv.fields_len);
	}

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
			s_info.last_noc_status, s_info.last_noc_index,
			(unsigned int)(s_info.fabrics[0].node_id >> 32),
			(unsigned int)s_info.fabrics[0].node_id,
			(unsigned int)(s_info.fabrics[0].fabric_id >> 32),
			(unsigned int)s_info.fabrics[0].fabric_id);
	}
	if (resp_len == 0u) {
		/* The command ran; the peer asked not to be told. */
		LOG_INF("invoke done, response suppressed");
		return;
	}

	send_im(MATTER_IM_OP_INVOKE_COMMAND_RESPONSE, s_report, resp_len);
	LOG_DBG("InvokeResponse: %u B", (unsigned int)resp_len);
}

/**
 * Apply a WriteRequest.
 *
 * The commissioner's last act, and the one this node used to answer with
 * silence: an ACL entry granting itself Administer over CASE. A home app that
 * has finished commissioning and cannot record that it owns the node sits on
 * "Adding to home" until it gives up.
 */
static void on_write_request(const struct matter_exchange_in *in)
{
	static struct matter_im_write wr;
	size_t resp_len = 0u;
	int rc;

	rc = matter_im_write_request_decode(in->payload, in->payload_len, &wr);
	if (rc != MATTER_OK) {
		LOG_WRN("unreadable WriteRequest (%d), %u B", rc, (unsigned int)in->payload_len);
		return;
	}
	LOG_INF("  write: endpoint %u cluster 0x%04x attribute 0x%04x, %u B", wr.path.endpoint,
		(unsigned int)wr.path.cluster, (unsigned int)wr.path.attribute,
		(unsigned int)wr.data_len);

	rc = matter_im_write_response_encode(&s_im, &wr, s_report, sizeof(s_report), &resp_len);
	if (rc != MATTER_OK) {
		LOG_ERR("cannot build WriteResponse (%d)", rc);
		return;
	}
	if (resp_len == 0u) {
		/* The write ran; the peer asked not to be told. */
		LOG_INF("  write done, response suppressed");
		return;
	}
	send_im(MATTER_IM_OP_WRITE_RESPONSE, s_report, resp_len);
}

/**
 * The subscription this node is serving, if any.
 *
 * One. Apple opens exactly one during commissioning, and a table of them would
 * be RAM spent on a case that has not arrived.
 */
static struct {
	struct matter_im_read read;
	uint32_t id;
	uint16_t max_interval_s;
	/** Reports already delivered by earlier chunks of the priming report. */
	uint16_t sent;
	/** More chunks remain; the next StatusResponse asks for one. */
	bool more;
	/* Between the priming report and the StatusResponse that confirms it. */
	bool priming;
	bool active;
} s_sub;

/**
 * Send one chunk of the priming report.
 *
 * The whole data model does not fit one Matter message -- the spec caps a
 * message at the IPv6 MTU and this node's answer measured 1479 bytes of payload
 * against a ~1232 byte ceiling. An oversized datagram is not slow, it is never
 * delivered, and the subscriber re-subscribes forever with nothing to say why.
 */
static void send_report_chunk(void)
{
	struct matter_im_report_stats stats;
	size_t report_len = 0u;
	uint16_t emitted = 0u;
	int rc;

	rc = matter_im_report_data_chunk(&s_im, &s_sub.read, s_sub.sent, s_report,
					 MATTER_MAX_MESSAGE_LEN, &report_len, &s_sub.more, &emitted,
					 &stats);
	if (rc != MATTER_OK) {
		LOG_ERR("cannot build the report chunk (%d)", rc);
		return;
	}
	if (emitted == 0u && s_sub.more) {
		/* Not a chunk boundary -- one report is larger than a whole
		 * message, and no number of chunks will help. */
		LOG_ERR("a single attribute does not fit a message; giving up");
		s_sub.more = false;
		return;
	}
	s_sub.sent += emitted;
	LOG_DBG("  chunk %u B, %u report(s), %u total, %s", (unsigned int)report_len, emitted,
		s_sub.sent, s_sub.more ? "MORE" : "last");
	send_im(MATTER_IM_OP_REPORT_DATA, s_report, report_len);
}

/**
 * Begin a subscription.
 *
 * The order is not the obvious one. A SubscribeRequest is answered with the
 * REPORT, not with the SubscribeResponse: the subscriber acknowledges that
 * report with a StatusResponse, and only then is the SubscribeResponse sent
 * (ReadHandler.cpp:240-250). Answering the request directly leaves the
 * subscriber holding an id for a subscription whose initial values never
 * arrived, which is indistinguishable from a node that stopped reporting.
 */
static void on_subscribe_request(const struct matter_exchange_in *in)
{
	static struct matter_im_subscribe sub;
	int rc;

	rc = matter_im_subscribe_request_decode(in->payload, in->payload_len, &sub);
	if (rc != MATTER_OK) {
		LOG_WRN("unreadable SubscribeRequest (%d), %u B", rc, (unsigned int)in->payload_len);
		return;
	}

	s_sub.read = sub.read;
	/*
	 * Any non-zero id will do -- it is this node's handle and the subscriber
	 * only ever echoes it back. Counted rather than random so two
	 * subscriptions in one boot cannot collide, and never zero because zero
	 * is how a plain read is told apart from a priming report.
	 */
	static uint32_t next_id;

	s_sub.id = ++next_id;
	/*
	 * The ceiling is the subscriber's limit, not a request: reporting later
	 * than this is what makes a subscription dead. Committing to it exactly
	 * is honest only if this node then reports on time -- see the note in
	 * on_status_response().
	 */
	s_sub.max_interval_s = sub.max_interval_s;
	s_sub.read.subscription_id = s_sub.id;
	s_sub.priming = true;
	s_sub.active = false;

	LOG_INF("  subscribe: %u path(s), %u..%u s, id 0x%08x", s_sub.read.n_paths,
		sub.min_interval_s, sub.max_interval_s, (unsigned int)s_sub.id);
	/*
	 * WHICH path, not just how many. A subscription to something this node
	 * answers with silence produces a priming report that is structurally
	 * perfect and empty, and a subscriber waiting on a value that will never
	 * come looks exactly like a subscriber that never got the report.
	 */
	for (uint8_t i = 0; i < s_sub.read.n_paths; i++) {
		const struct matter_im_path *p = &s_sub.read.paths[i];

		LOG_INF("  sub[%u] endpoint %d cluster 0x%04x attribute 0x%04x", i,
			p->have_endpoint ? (int)p->endpoint : -1,
			p->have_cluster ? (unsigned int)p->cluster : 0xFFFFu,
			p->have_attribute ? (unsigned int)p->attribute : 0xFFFFu);
	}

	s_sub.sent = 0u;
	s_sub.more = false;
	send_report_chunk();
}

/**
 * The subscriber acknowledged the priming report, so the subscription exists.
 *
 * The StatusResponse is not inspected beyond its arrival: a subscriber that
 * rejected the report would say so by not sending one.
 */
static void on_status_response(const struct matter_exchange_in *in)
{
	size_t resp_len = 0u;

	ARG_UNUSED(in);

	/*
	 * Between chunks this is the peer asking for the next one, not the
	 * acknowledgement that ends the priming report. Only the LAST chunk's
	 * StatusResponse establishes the subscription.
	 */
	if (s_sub.more) {
		send_report_chunk();
		return;
	}
	if (!s_sub.priming) {
		return;
	}
	s_sub.priming = false;
	s_sub.active = true;

	if (matter_im_subscribe_response_encode(s_sub.id, s_sub.max_interval_s, s_report,
						sizeof(s_report), &resp_len) != MATTER_OK) {
		LOG_ERR("cannot build the SubscribeResponse");
		return;
	}
	LOG_INF("  subscription 0x%08x ESTABLISHED, max interval %u s", (unsigned int)s_sub.id,
		s_sub.max_interval_s);
	/*
	 * NOT YET PERIODIC. Nothing here reports again, so this subscription
	 * lapses once max_interval_s passes and the subscriber is entitled to
	 * call the node unresponsive. Enough to finish commissioning, and
	 * flagged because a lapsed subscription looks exactly like a node that
	 * has gone away.
	 */
	send_im(MATTER_IM_OP_SUBSCRIBE_RESPONSE, s_report, resp_len);
}

static void on_secure(const struct matter_exchange_in *in)
{
	/*
	 * A bare acknowledgement asks nothing. Named here rather than left to
	 * the unhandled case at the bottom, which spent three log lines per ack
	 * -- and there is one after every message -- reporting that nothing
	 * needed doing. That is what kept filling the trace ring.
	 */
	if (in->protocol_id == MATTER_PROTOCOL_SECURE_CHANNEL &&
	    in->opcode == MATTER_SC_OP_ACK) {
		return;
	}

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
	if (in->protocol_id == MATTER_PROTOCOL_INTERACTION_MODEL &&
	    in->opcode == MATTER_IM_OP_WRITE_REQUEST) {
		on_write_request(in);
		return;
	}
	if (in->protocol_id == MATTER_PROTOCOL_INTERACTION_MODEL &&
	    in->opcode == MATTER_IM_OP_SUBSCRIBE_REQUEST) {
		on_subscribe_request(in);
		return;
	}
	if (in->protocol_id == MATTER_PROTOCOL_INTERACTION_MODEL &&
	    in->opcode == MATTER_IM_OP_STATUS_RESPONSE) {
		on_status_response(in);
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
	uint8_t noc_pub[MATTER_CASE_PUBKEY_LEN];
	bool have_noc_pub;
	/*
	 * The initiator's ephemeral key, kept because TBSData3 has to be rebuilt
	 * from it and the Sigma1 that carried it is long gone by then.
	 */
	uint8_t init_eph_pub[MATTER_CASE_PUBKEY_LEN];
	/**
	 * Which fabric this handshake is for, chosen by whichever one's
	 * destination identifier the Sigma1 matched. With two administrators
	 * there are two, and every later step -- the NOC to send, the key to
	 * sign with, the node id in the nonce -- belongs to that one.
	 */
	const struct matter_fabric *fabric;
	/*
	 * The transcript, as a running hash rather than the messages themselves.
	 * Sigma2's salt needs SHA-256(Sigma1), Sigma3's needs
	 * SHA-256(Sigma1 || Sigma2) and the session keys need all three -- and
	 * keeping the ~1.2 KB of messages to re-hash is not affordable here.
	 * Copy the context and finalise the copy to read an intermediate digest;
	 * finalising this one would end the transcript a message early.
	 */
	struct aliro_sha256 transcript;
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
			  const struct matter_proto_header *req,
			  const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)
{
	/* The fabric the Sigma1's destination identifier chose. */
	const struct matter_fabric *f = s_case.fabric;
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
	/* A new handshake supersedes any session the last one established, so
	 * the Sigma3 that follows THIS Sigma2 must be verified rather than
	 * answered from the old session's StatusReport. */
	s_case_ready = false;

	/* Start the transcript, and read SHA-256(Sigma1) off a COPY so the
	 * running context stays open for Sigma2 and Sigma3. */
	aliro_sha256_init(&s_case.transcript);
	aliro_sha256_update(&s_case.transcript, sigma1, sigma1_len);
	{
		struct aliro_sha256 snapshot = s_case.transcript;

		aliro_sha256_final(&snapshot, transcript);
	}
	memcpy(s_case.init_eph_pub, s1->initiator_pubkey, sizeof(s_case.init_eph_pub));

	/*
	 * The one assumption nothing has ever checked: that the key this signs
	 * with is the key the NOC certifies. Sigma2 is signed with op_priv and
	 * carries the NOC; if they disagree the peer verifies a signature
	 * against the wrong public key, fails, and says nothing -- which is
	 * indistinguishable from every other way this can go wrong.
	 */
	{
		struct matter_cert_info ci;
		uint8_t derived[MATTER_CASE_PUBKEY_LEN];

		if (aliro_ec_p256_pub_from_priv(f->op_priv, derived) == 0 &&
		    matter_cert_parse(f->noc, f->noc_len, &ci) == MATTER_OK &&
		    ci.have_public_key) {
			LOG_INF("  signing key %s the NOC; noc %u B, icac %u B",
				memcmp(derived, ci.public_key, sizeof(derived)) == 0
					? "MATCHES"
					: "does NOT match",
				(unsigned int)f->noc_len, (unsigned int)f->icac_len);
			/* Kept so the Sigma2 signature can be verified against
			 * the certificate it ships with. */
			memcpy(s_case.noc_pub, ci.public_key, sizeof(s_case.noc_pub));
			s_case.have_noc_pub = true;
		} else {
			s_case.have_noc_pub = false;
			LOG_WRN("  could not check the signing key against the NOC");
		}
	}

	memset(&in, 0, sizeof(in));
	in.initiator_pubkey = s1->initiator_pubkey;
	in.transcript_hash = transcript;
	in.ipk = ipk;
	in.noc = f->noc;
	in.noc_len = f->noc_len;
	in.icac = f->icac_len > 0u ? s_info.icac.buf : NULL;
	in.icac_len = f->icac_len;
	in.op_priv = f->op_priv;
	in.responder_random = s_case.responder_random;
	in.responder_eph_priv = s_case.eph_priv;
	in.responder_eph_pub = s_case.eph_pub;
	in.resumption_id = s_case.resumption_id;
	in.responder_session_id = s_case.local_session_id;
	in.verify_pub = s_case.have_noc_pub ? s_case.noc_pub : NULL;

	/* Framed after both headers, so the payload lands where it will be sent
	 * from rather than being copied into place afterwards. */
	/*
	 * A DESTINATION node id, and no source. Not symmetry with the Sigma1:
	 * the ephemeral node id belongs to the INITIATOR, and the two directions
	 * carry it in different fields. SessionManager.cpp:296-302 sets the
	 * source on an initiator's message and the destination on a responder's,
	 * both to the same value; the receive side at :763 rejects outright any
	 * unsecured message carrying both or neither.
	 *
	 * Answering with a source node id is what a fresh initiator looks like,
	 * so the peer allocates a NEW unauthenticated session for it
	 * (:772-776 "Assume peer is the initiator, we are the responder") rather
	 * than matching the CASE session waiting on this exchange. The Sigma2 is
	 * then an unsolicited message with no handler, which ExchangeMgr.cpp:411
	 * acknowledges and drops. That is the whole observed symptom: a
	 * StandaloneAck, no StatusReport, and a fresh Sigma1 once the peer's
	 * session times out. None of the payload is ever looked at, which is why
	 * every byte inside it verified and none of it helped.
	 */
	mh.flags = MATTER_MSG_DSIZ_NODE;
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
	mh.source_node_id = 0u;
	/*
	 * The initiator's EPHEMERAL id, echoed from the Sigma1's source field --
	 * not this node's operational node id. It is what keys the peer's
	 * unauthenticated session table, and it is random per handshake, so
	 * there is nothing to derive it from except the message that carried it.
	 */
	mh.dest_node_id = req_mh->source_node_id;
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
	ph.ack_counter = req_mh->message_counter;
	rc = matter_proto_header_encode(&ph, reply + mh_len, cap - mh_len, &ph_len);
	if (rc != MATTER_OK) {
		LOG_ERR("  cannot frame Sigma2 protocol header (%d)", rc);
		return 0u;
	}

	rc = matter_case_sigma2_encode(&in, reply + mh_len + ph_len, cap - mh_len - ph_len, &s2_len,
				       s_case.shared);
	memset(transcript, 0, sizeof(transcript));
	if (rc != MATTER_OK) {
		/* MATTER_E_STATE here now means the signature did not verify
		 * against the NOC's own public key, which is a far more specific
		 * thing than "could not be built". */
		LOG_ERR("  Sigma2 NOT built (%d)%s", rc,
			rc == MATTER_E_STATE ? " -- crypto step failed (sign/verify/ECDH)" : "");
		return 0u;
	}

	s_case.active = true;
	/* The transcript covers payloads, never headers -- the same rule the
	 * Sigma1 length check above exists to prove. */
	aliro_sha256_update(&s_case.transcript, reply + mh_len + ph_len, s2_len);
	/*
	 * The first 48 bytes are the whole TLV skeleton: outer structure, the
	 * random, the session id, and the start of the ephemeral key. Enough to
	 * decode offline and settle whether the SHAPE is right, which is the
	 * question no amount of staring at the encoder answers -- and far
	 * cheaper than another pairing attempt.
	 */
	LOG_HEXDUMP_DBG(reply + mh_len + ph_len, s2_len < 48u ? s2_len : 48u, "sigma2 head");
	LOG_INF("  Sigma2 out: %u B payload, %u B total, session 0x%04x", (unsigned int)s2_len,
		(unsigned int)(mh_len + ph_len + s2_len), (unsigned int)s_case.local_session_id);
	return mh_len + ph_len + s2_len;
}

static size_t case_status_report(const struct matter_proto_header *req,
				 const struct matter_msg_header *req_mh, uint8_t *reply,
				 size_t cap);

/**
 * Answer a Sigma3, which ends the handshake.
 *
 * Sigma2 asked the initiator to believe this node; Sigma3 is the initiator
 * proving the same thing back, and it is the last message either side sends in
 * the clear. What follows it is encrypted under keys neither side transmitted,
 * so a mistake here surfaces as silence on the NEXT message rather than as a
 * failure on this one -- which is the reason for the checks logged below.
 */
static size_t handle_sigma3(const uint8_t *sigma3, size_t sigma3_len, const uint8_t *ipk,
			    const struct matter_proto_header *req,
			    const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)
{
	struct matter_case_sigma3_in in;
	struct matter_case_sigma3_out peer;
	struct matter_session_keys keys;
	uint8_t salt[MATTER_CASE_IPK_LEN + 32u];
	uint8_t digest[32];
	int rc;

	if (!s_case.active) {
		LOG_WRN("  Sigma3 with no handshake in progress");
		return 0u;
	}
	/*
	 * A retransmitted Sigma3. Re-verifying is impossible as well as
	 * pointless: the transcript that verified the first one was finalised to
	 * derive the session keys, and SHA-256 cannot be finalised twice. The
	 * peer resent because it never saw the StatusReport, so send that again
	 * and touch nothing else.
	 */
	if (s_case_ready) {
		LOG_DBG("  Sigma3 again -- resending the StatusReport");
		return case_status_report(req, req_mh, reply, cap);
	}

	/* SHA-256(Sigma1 || Sigma2), taken off a COPY so the running context can
	 * go on to absorb this Sigma3 for the session keys below. */
	{
		struct aliro_sha256 snapshot = s_case.transcript;

		aliro_sha256_final(&snapshot, digest);
	}

	memset(&in, 0, sizeof(in));
	in.shared = s_case.shared;
	in.ipk = ipk;
	in.transcript_hash = digest;
	in.initiator_eph_pub = s_case.init_eph_pub;
	in.responder_eph_pub = s_case.eph_pub;

	rc = matter_case_sigma3_open(&in, sigma3, sigma3_len, &peer);
	if (rc != MATTER_OK) {
		/* Worth separating: a failed AEAD tag means the key schedule
		 * diverged, a failed signature means it did NOT and the identity
		 * is the problem. Both return MATTER_E_TYPE, so this only
		 * narrows it -- but it narrows it to two. */
		LOG_ERR("  Sigma3 REJECTED (%d)%s", rc,
			rc == MATTER_E_TYPE ? " -- AEAD tag or signature failed" : "");
		return 0u;
	}
	LOG_INF("  Sigma3 VERIFIED: peer node 0x%08x%08x", (unsigned int)(peer.node_id >> 32),
		(unsigned int)peer.node_id);

	/*
	 * The signature proved the sender holds the key its NOC names. This is
	 * what ties that NOC to THIS fabric rather than to another fabric the
	 * same phone also belongs to.
	 */
	if (s_case.fabric == NULL || peer.fabric_id != s_case.fabric->fabric_id) {
		LOG_ERR("  Sigma3 names a different fabric -- refusing");
		return 0u;
	}

	/* Only now can the transcript close, over all three messages. */
	aliro_sha256_update(&s_case.transcript, sigma3, sigma3_len);
	aliro_sha256_final(&s_case.transcript, digest);
	memcpy(salt, ipk, MATTER_CASE_IPK_LEN);
	memcpy(&salt[MATTER_CASE_IPK_LEN], digest, sizeof(digest));
	rc = matter_derive_session_keys(s_case.shared, MATTER_CASE_SECRET_LEN, salt, sizeof(salt),
					false, &keys);
	memset(salt, 0, sizeof(salt));
	memset(digest, 0, sizeof(digest));
	if (rc != MATTER_OK) {
		LOG_ERR("  session keys NOT derived (%d)", rc);
		return 0u;
	}

	/*
	 * A fresh exchange object, not the BLE one: this session has its own
	 * counter space, and a counter reused under a new key repeats an AEAD
	 * nonce. The exchange id is released with it, because the commissioner
	 * opens a new exchange for what comes next.
	 */
	{
		uint32_t seed = 0u;

		/* Drawn, not carried over from s_case_counter: that one is the
		 * UNSECURED counter and is on the wire in clear text, so seeding
		 * from it would let a listener predict this session's. */
		if (aliro_random((uint8_t *)&seed, sizeof(seed)) != 0) {
			LOG_ERR("  no entropy for the session counter");
			memset(&keys, 0, sizeof(keys));
			return 0u;
		}
		matter_exchange_init(&s_case_x, seed, true);
		rc = matter_exchange_promote(&s_case_x, s_case.local_session_id,
					     s_case.peer_session_id, &keys, seed);
		/*
		 * The nonces. Unlike PASE, a CASE session builds them from the
		 * two OPERATIONAL node ids -- ours when sealing, the peer's when
		 * opening -- and neither appears in any header. Getting this
		 * wrong produces messages that decrypt to nothing with no error
		 * anyone can report.
		 */
		matter_exchange_set_op_node_ids(&s_case_x, s_case.fabric->node_id, peer.node_id);
	}
	memset(&keys, 0, sizeof(keys));
	if (rc != MATTER_OK) {
		LOG_ERR("  CASE session NOT installed (%d)", rc);
		return 0u;
	}
	s_case_ready = true;
	/* What unblocks CommissioningComplete. The cluster layer cannot see a
	 * session, so this is the only place that can say so. */
	s_info.case_established = true;
	LOG_INF("  CASE ESTABLISHED: local session 0x%04x, peer 0x%04x",
		(unsigned int)s_case.local_session_id, (unsigned int)s_case.peer_session_id);
#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
	/* The third report point. main.c reads the peak at an unlock grant and
	 * prov_shell.c at an import, and both predate this node: Sigma3 is now
	 * the heaviest crypto this image does, running an ECDH, a signature
	 * verify and three HKDF expansions before the session keys exist. The
	 * peak is cumulative since boot, so reading it HERE covers PASE and both
	 * earlier Sigma stages too. Sizing MBEDTLS_HEAP_SIZE off the unlock
	 * figure alone would miss all of it. */
	{
		size_t used = 0;
		size_t blocks = 0;

		mbedtls_memory_buffer_alloc_max_get(&used, &blocks);
		LOG_INF("  mbedtls heap peak @case: %u B of %u (%u blocks)", (unsigned int)used,
			(unsigned int)CONFIG_MBEDTLS_HEAP_SIZE, (unsigned int)blocks);
	}
#endif

	return case_status_report(req, req_mh, reply, cap);
}

/**
 * The StatusReport that ends CASE.
 *
 * Still unsecured and still addressed to the initiator's ephemeral id: this is
 * the last message before the keys take effect, not the first one after.
 */
static size_t case_status_report(const struct matter_proto_header *req,
				 const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)
{
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	size_t mh_len = 0u;
	size_t ph_len = 0u;
	size_t sr_len = 0u;

	mh.flags = MATTER_MSG_DSIZ_NODE;
	mh.session_id = 0u;
	mh.security_flags = 0u;
	mh.message_counter = ++s_case_counter;
	mh.source_node_id = 0u;
	mh.dest_node_id = req_mh->source_node_id;
	mh.dest_group_id = 0u;
	if (matter_msg_header_encode(&mh, reply, cap, &mh_len) != MATTER_OK) {
		return 0u;
	}

	ph.exchange_flags = MATTER_EX_FLAG_A | MATTER_EX_FLAG_R;
	ph.opcode = MATTER_SC_OP_STATUS_REPORT;
	ph.exchange_id = req->exchange_id;
	ph.vendor_id = 0u;
	ph.protocol_id = MATTER_PROTOCOL_SECURE_CHANNEL;
	ph.ack_counter = req_mh->message_counter;
	if (matter_proto_header_encode(&ph, reply + mh_len, cap - mh_len, &ph_len) != MATTER_OK) {
		return 0u;
	}
	if (matter_sc_status_report(MATTER_SC_CODE_SUCCESS, reply + mh_len + ph_len,
				    cap - mh_len - ph_len, &sr_len) != MATTER_OK) {
		return 0u;
	}
	LOG_DBG("  StatusReport success out: %u B total", (unsigned int)(mh_len + ph_len + sr_len));
	return mh_len + ph_len + sr_len;
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

	/*
	 * A non-zero session id means the rest is encrypted, INCLUDING the
	 * protocol header -- so this has to branch before anything tries to read
	 * an opcode out of it. Decoding first is how "protocol 0xe65a opcode
	 * 0xc1" ends up in a log: those are ciphertext bytes being read as a
	 * header.
	 */
	if (mh.session_id != 0u) {
		struct matter_exchange_in in;

		if (!s_case_ready || mh.session_id != s_case.local_session_id) {
			LOG_WRN("  encrypted for session 0x%04x, which is not ours",
				(unsigned int)mh.session_id);
			return 0u;
		}
		s_thread_reply = reply;
		s_thread_reply_cap = cap;
		s_thread_reply_len = 0u;

		rc = matter_exchange_recv(&s_case_x, msg, len, &in, s_pt, sizeof(s_pt));
		if (rc == MATTER_E_DUP) {
			(void)matter_exchange_standalone_ack(&s_case_x, s_out, sizeof(s_out),
							     &s_thread_reply_len);
			if (s_thread_reply_len > cap) {
				s_thread_reply_len = 0u;
			} else {
				memcpy(reply, s_out, s_thread_reply_len);
			}
		} else if (rc != MATTER_OK) {
			LOG_WRN("  CASE message refused (%d)", rc);
			s_thread_reply_len = 0u;
		} else {
			LOG_DBG("  CASE in: protocol 0x%04x opcode 0x%02x, %u B",
				(unsigned int)in.protocol_id, in.opcode,
				(unsigned int)in.payload_len);
			on_secure(&in);
		}

		s_thread_reply = NULL;
		return s_thread_reply_len;
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
	LOG_DBG("  protocol 0x%04x opcode 0x%02x exchange 0x%04x", (unsigned int)ph.protocol_id,
		ph.opcode, (unsigned int)ph.exchange_id);
	/* Demoted now that the addressing is proven: three lines per datagram
	 * fills the 4 KB trace ring inside one handshake, and a full ring looks
	 * exactly like a board that logged nothing. */
	LOG_DBG("  in hdr: flags 0x%02x sec 0x%02x ctr %u exflags 0x%02x", mh.flags,
		mh.security_flags, (unsigned int)mh.message_counter, ph.exchange_flags);
	LOG_DBG("  in hdr: source node 0x%08x%08x", (unsigned int)(mh.source_node_id >> 32),
		(unsigned int)mh.source_node_id);

	if (ph.protocol_id != MATTER_PROTOCOL_SECURE_CHANNEL ||
	    (ph.opcode != MATTER_OP_CASE_SIGMA1 && ph.opcode != MATTER_OP_CASE_SIGMA3)) {
		return 0u;
	}

	if (s_info.fabrics[0].index == 0u) {
		LOG_WRN("  no fabric to match it against");
		return 0u;
	}
	/*
	 * Sigma3 continues the handshake Sigma1 chose a fabric for. Deriving the
	 * key from a different one would decrypt to nothing, so the choice is
	 * made once, in the Sigma1 branch, and reused here.
	 */
	if (ph.opcode == MATTER_OP_CASE_SIGMA3) {
		if (s_case.fabric == NULL) {
			LOG_WRN("  Sigma3 with no fabric chosen");
			return 0u;
		}
		if (matter_fabric_compressed_id(s_case.fabric->root_public_key,
						s_case.fabric->fabric_id, cfid) != MATTER_OK ||
		    matter_case_operational_ipk(s_case.fabric->ipk, cfid, ipk) != MATTER_OK) {
			LOG_ERR("  could not derive the operational key");
			return 0u;
		}
	}
	/*
	 * The reply is addressed to the id in this field, so its absence is not
	 * a detail to skip past: there would be nothing to address the answer to
	 * and the peer would drop it exactly as it dropped the ones before this.
	 */
	if ((mh.flags & MATTER_MSG_FLAG_S) == 0u) {
		LOG_WRN("  no source node id -- cannot address a reply");
		return 0u;
	}

	if (ph.opcode == MATTER_OP_CASE_SIGMA3) {
		LOG_DBG("  lengths: msg %u = hdr %u + proto %u + payload %u", (unsigned int)len,
			(unsigned int)mh_len, (unsigned int)ph_len,
			(unsigned int)(len - mh_len - ph_len));
		return handle_sigma3(msg + mh_len + ph_len, len - mh_len - ph_len, ipk, &ph, &mh,
				     reply, cap);
	}

	rc = matter_case_sigma1_decode(msg + mh_len + ph_len, len - mh_len - ph_len, &s1);
	if (rc != MATTER_OK) {
		LOG_WRN("  Sigma1 unreadable (%d)", rc);
		return 0u;
	}
	LOG_INF("  Sigma1: initiator session 0x%04x, resumption %s",
		(unsigned int)s1.initiator_session_id, s1.has_resumption ? "offered" : "none");
	/*
	 * The transcript hash is over the Sigma1 payload and nothing else, so
	 * these three have to sum to the datagram. A payload length that is one
	 * byte long or short still decodes -- the TLV ends where it ends -- and
	 * still yields the right destination identifier, but hashes to something
	 * the peer never computed. That failure is completely silent, and this
	 * is the only place it is visible.
	 */
	LOG_DBG("  lengths: msg %u = hdr %u + proto %u + payload %u", (unsigned int)len,
		(unsigned int)mh_len, (unsigned int)ph_len, (unsigned int)(len - mh_len - ph_len));

	/*
	 * WHICH fabric, by trying each. The destination identifier is an HMAC
	 * under a fabric's operational key over the identity being asked for, so
	 * the only way to learn which fabric an initiator means is to recompute
	 * it for each one and look for a match. That is also what stops an
	 * unsolicited Sigma1 enumerating this node's fabrics: get the key wrong
	 * and you learn nothing.
	 */
	s_case.fabric = NULL;
	for (size_t fi = 0u; fi < MATTER_SUPPORTED_FABRICS; fi++) {
		const struct matter_fabric *f = &s_info.fabrics[fi];

		if (f->index == 0u) {
			continue;
		}
		if (matter_fabric_compressed_id(f->root_public_key, f->fabric_id, cfid) !=
			    MATTER_OK ||
		    matter_case_operational_ipk(f->ipk, cfid, ipk) != MATTER_OK ||
		    matter_case_destination_id(ipk, s1.initiator_random, f->root_public_key,
					       f->fabric_id, f->node_id, want) != MATTER_OK) {
			LOG_ERR("  could not recompute the destination identifier");
			return 0u;
		}
		if (memcmp(want, s1.destination_id, sizeof(want)) == 0) {
			s_case.fabric = f;
			break;
		}
	}
	if (s_case.fabric == NULL) {
		LOG_WRN("  destination matches NO fabric this node holds");
		return 0u;
	}
	LOG_INF("  destination MATCHES fabric %u -- answering", s_case.fabric->index);

	return send_sigma2(&s1, ipk, msg + mh_len + ph_len, len - mh_len - ph_len, &ph, &mh, reply,
			   cap);
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
	ecdh_known_answer_test();

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
