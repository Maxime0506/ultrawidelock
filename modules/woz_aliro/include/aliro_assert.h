/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_assert — the presence-assertion protocol for the non-door primitive.
 *
 * A USB presence dongle answers a host challenge with a signed statement:
 * "a provisioned credential is within N cm right now, in response to THIS
 * nonce." The USB link is treated as hostile, so the statement is authenticated
 * with HMAC-SHA256 under a symmetric key paired at install time (held by the
 * dongle in NVS and by the host in a root-only file). Both endpoints are under
 * the user's control and there is no third party, so a symmetric MAC is the
 * right primitive -- and it is fully host-testable, unlike an asymmetric scheme.
 *
 * Anti-replay is the nonce: the host mints a fresh CSPRNG nonce per challenge,
 * accepts one response for it, then forgets it. A captured/replayed assertion
 * carries a stale nonce and is rejected. A strictly-increasing dongle uptime is
 * an optional second guard (forward-progress) the host can enforce.
 *
 * This module is the wire codec + verifier only. It knows nothing about UWB or
 * BLE; the dongle firmware fills the fields from a real Aliro ranging round and
 * the host maps the verdict to a PAM decision. Portable C11 (SHA/HMAC via
 * aliro_hash.c), so the exact codec is host-KAT'd and fuzzed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIRO_ASSERT_NONCE_LEN  16u
#define ALIRO_ASSERT_CREDID_LEN 8u      /* first 8 bytes of SHA-256(credential pub) */
#define ALIRO_ASSERT_MAC_LEN    32u     /* HMAC-SHA256 */
#define ALIRO_ASSERT_KEY_LEN    32u     /* symmetric pairing key */
#define ALIRO_ASSERT_WIRE_LEN   70u     /* fixed frame size, see the layout below */
#define ALIRO_ASSERT_DIST_NONE  0xFFFFu /* distance_cm sentinel: no valid range */

/* Presence status the dongle reports from the ranging round. */
enum aliro_assert_status {
	ALIRO_PRESENCE_ABSENT = 0,  /* no provisioned credential answered the round */
	ALIRO_PRESENCE_PRESENT = 1, /* a trusted credential transacted + ranged */
};

/*
 * The assertion fields. Wire layout (big-endian multi-byte), MAC covers every
 * byte before it:
 *   magic(2)=A1 50 | version(1)=01 | status(1) | nonce(16) | cred_id(8) |
 *   distance_cm(2) | uptime_ms(8) | mac(32)  = 70 bytes.
 */
struct aliro_assert {
	uint8_t status;                           /* enum aliro_assert_status */
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];    /* echoed from the challenge */
	uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN]; /* which credential answered */
	uint16_t distance_cm;                     /* ALIRO_ASSERT_DIST_NONE if none */
	uint64_t uptime_ms;                       /* dongle monotonic ms at build */
};

/* Verdict / reason codes from aliro_assert_verify. 0 = presence confirmed;
 * every negative value is a distinct reject reason (for logging + tests). */
enum aliro_assert_verdict {
	ALIRO_ASSERT_OK = 0,
	ALIRO_ASSERT_E_MALFORMED = -1, /* bad length, magic, or version */
	ALIRO_ASSERT_E_MAC = -2,       /* HMAC mismatch: wrong key or tampered */
	ALIRO_ASSERT_E_NONCE = -3,     /* nonce != challenge: replay / mismatch */
	ALIRO_ASSERT_E_STALE = -4,     /* uptime_ms <= min_uptime_ms */
	ALIRO_ASSERT_E_ABSENT = -5,    /* status != PRESENT */
	ALIRO_ASSERT_E_RANGE = -6,     /* distance_cm > threshold, or no range */
};

/* cred_id = first 8 bytes of SHA-256(cred_pub[65]). Stable id for a credential
 * public key so the host can bind presence to a specific enrolled credential. */
void aliro_assert_cred_id(const uint8_t cred_pub[65], uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN]);

/* Serialise + MAC an assertion into a 70-byte wire frame under key. Returns 0
 * and sets *wire_len; -1 if wire_cap < ALIRO_ASSERT_WIRE_LEN. */
int aliro_assert_build(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const struct aliro_assert *a,
		       uint8_t *wire, size_t wire_cap, size_t *wire_len);

/*
 * Parse + fully verify a wire frame. Checks, in order: length/magic/version,
 * HMAC (constant-time), nonce echo, forward-progress (uptime_ms > min_uptime_ms;
 * pass 0 to skip), status == PRESENT, distance_cm <= threshold_cm.
 *
 * Returns ALIRO_ASSERT_OK (0) only when all pass; otherwise the first failing
 * reason. *out, when non-NULL, is populated with whatever parsed AFTER the MAC
 * check passed (for logging / cred-allowlist); it is left untouched if the MAC
 * or framing is bad, and must never be trusted for an unlock on a non-zero
 * return. threshold_cm is inclusive.
 */
int aliro_assert_verify(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const uint8_t *wire,
			size_t wire_len, const uint8_t expected_nonce[ALIRO_ASSERT_NONCE_LEN],
			uint16_t threshold_cm, uint64_t min_uptime_ms, struct aliro_assert *out);

#ifdef __cplusplus
}
#endif
