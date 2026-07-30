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
#include "matter_commission.h"
#include "matter_exchange.h"
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

/** Framed reply: both headers plus the largest PASE message. */
static uint8_t s_out[MATTER_EXCHANGE_HEADER_MAX + MATTER_PASE_REPLY_MAX];

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

	matter_exchange_init(&s_exchange, counter_seed);
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
	rc = matter_ble_send(s_out, framed);
	if (rc != 0) {
		LOG_ERR("sending opcode 0x%02x rc=%d", opcode, rc);
	}
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

	rc = matter_exchange_recv(&s_exchange, msg, len, &in);
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
		LOG_WRN("refused a %u-byte message (%d)", (unsigned int)len, rc);
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

	if (matter_pase_responder_state(&s_pase) == MATTER_PASE_ST_DONE) {
		LOG_INF("PASE complete: session keys derived (peer session id 0x%04x)",
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

	matter_ble_set_link_handler(on_link_reset);
	matter_ble_set_msg_handler(on_message);
	return 0;
}
