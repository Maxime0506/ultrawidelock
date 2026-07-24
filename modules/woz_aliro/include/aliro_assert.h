/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_assert — the presence-assertion protocol for the non-door primitive.
 *
 * A presence dongle answers a challenge with a signed statement: "a provisioned
 * credential is within N cm right now, in response to THIS nonce." The link is
 * treated as hostile, so the statement is authenticated. Two algorithms, chosen
 * per frame by the alg byte, because the two use cases have different verifiers:
 *
 *   HMAC-SHA256  the paired-local path. A symmetric key installed at pairing
 *                time (dongle NVS, host root-only file). Cheapest, and the only
 *                thing needed when the verifier IS the paired host.
 *   ECDSA-P256   the portable path. Anyone holding the dongle's public key can
 *                verify, so an assertion becomes a proof a THIRD party accepts
 *                (a CI runner checking that a human was at the machine when a
 *                release was signed). P-256 rather than Ed25519 because the
 *                Aliro credential keys are already P-256 -- cred_pub below is a
 *                65-byte uncompressed point -- so this reuses curve code that is
 *                already on-target validated, instead of adding a second curve.
 *
 * Anti-replay is the nonce: the verifier mints a fresh CSPRNG nonce per
 * challenge, accepts one response for it, then forgets it. A captured assertion
 * carries a stale nonce and is rejected. A strictly-increasing dongle uptime is
 * an optional second guard (forward-progress) the verifier can enforce.
 *
 * uptime_ms is monotonic since dongle boot, which is enough to order two frames
 * from one session but says nothing to a third party about WHEN. unix_ms is the
 * dongle's attested wall clock for exactly that, and is ALIRO_ASSERT_TIME_NONE
 * on a dongle with no trusted time -- which is why it is a separate field and
 * not a replacement.
 *
 * This module is the wire codec + verifier only. It knows nothing about UWB or
 * BLE; the dongle firmware fills the fields from a real Aliro ranging round and
 * the host maps the verdict to a decision. Portable C11 (SHA/HMAC via
 * aliro_hash.c), so the exact codec is host-KAT'd and fuzzed.
 *
 * Wire version 2. Version 1 (70 bytes, HMAC-only, no alg byte, no wall clock)
 * was never flashed to a device, so no v1 decoder is kept.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIRO_ASSERT_NONCE_LEN  16u
#define ALIRO_ASSERT_CREDID_LEN 8u  /* first 8 bytes of SHA-256(credential pub) */
#define ALIRO_ASSERT_MAC_LEN    32u /* HMAC-SHA256 tag */
#define ALIRO_ASSERT_SIG_LEN    64u /* ECDSA-P256 signature, r||s, 32 bytes each */
#define ALIRO_ASSERT_KEY_LEN    32u /* symmetric pairing key */
#define ALIRO_ASSERT_PUB_LEN    65u /* uncompressed P-256 point, 0x04 || X || Y */

/* Signed prefix is identical for both algorithms; only the trailing tag differs. */
#define ALIRO_ASSERT_SIGNED_LEN 47u
#define ALIRO_ASSERT_WIRE_HMAC  (ALIRO_ASSERT_SIGNED_LEN + ALIRO_ASSERT_MAC_LEN) /* 79 */
#define ALIRO_ASSERT_WIRE_P256  (ALIRO_ASSERT_SIGNED_LEN + ALIRO_ASSERT_SIG_LEN) /* 111 */
#define ALIRO_ASSERT_WIRE_MAX   ALIRO_ASSERT_WIRE_P256 /* size any receive buffer by this */

#define ALIRO_ASSERT_DIST_NONE 0xFFFFu /* distance_cm sentinel: no valid range */
#define ALIRO_ASSERT_TIME_NONE 0u      /* unix_ms sentinel: no trusted wall clock */

/* Authentication algorithm, carried in the frame so the verifier never has to
 * guess and so a frame can never be verified under the algorithm it was not
 * built for. Values are wire-visible; do not renumber. */
enum aliro_assert_alg {
	ALIRO_ASSERT_ALG_HMAC_SHA256 = 1,
	ALIRO_ASSERT_ALG_ECDSA_P256 = 2,
};

/* Presence status the dongle reports from the ranging round. */
enum aliro_assert_status {
	ALIRO_PRESENCE_ABSENT = 0,  /* no provisioned credential answered the round */
	ALIRO_PRESENCE_PRESENT = 1, /* a trusted credential transacted + ranged */
};

/*
 * The assertion fields. Wire layout (big-endian multi-byte), the tag covers
 * every byte before it:
 *   magic(2)=A1 50 | version(1)=02 | alg(1) | status(1) | nonce(16) |
 *   cred_id(8) | distance_cm(2) | uptime_ms(8) | unix_ms(8) | tag(32 or 64)
 * = 79 bytes with an HMAC tag, 111 with a P-256 signature.
 */
struct aliro_assert {
	uint8_t status;                           /* enum aliro_assert_status */
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];    /* echoed from the challenge */
	uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN]; /* which credential answered */
	uint16_t distance_cm;                     /* ALIRO_ASSERT_DIST_NONE if none */
	uint64_t uptime_ms;                       /* dongle monotonic ms at build */
	uint64_t unix_ms;                         /* attested wall clock, or TIME_NONE */
};

/* Verdict / reason codes from aliro_assert_verify. 0 = presence confirmed;
 * every negative value is a distinct reject reason (for logging + tests). */
enum aliro_assert_verdict {
	ALIRO_ASSERT_OK = 0,
	ALIRO_ASSERT_E_MALFORMED = -1, /* bad length, magic, or version */
	ALIRO_ASSERT_E_MAC = -2,       /* tag mismatch: wrong key or tampered */
	ALIRO_ASSERT_E_NONCE = -3,     /* nonce != challenge: replay / mismatch */
	ALIRO_ASSERT_E_STALE = -4,     /* uptime_ms <= min_uptime_ms */
	ALIRO_ASSERT_E_ABSENT = -5,    /* status != PRESENT */
	ALIRO_ASSERT_E_RANGE = -6,     /* distance_cm > threshold, or no range */
	ALIRO_ASSERT_E_ALG = -7,       /* unknown alg, or not the one being verified */
};

/* Frame length for an algorithm, or 0 if the algorithm is unknown. The alg byte
 * sits at a fixed offset inside the signed prefix, so a stream scanner can read
 * it at a candidate offset and learn how long the frame is before validating. */
size_t aliro_assert_wire_len(uint8_t alg);

/* Reads the alg byte of a candidate frame. buf must hold at least
 * ALIRO_ASSERT_SIGNED_LEN bytes; returns 0 (an invalid alg) otherwise. */
uint8_t aliro_assert_peek_alg(const uint8_t *buf, size_t len);

/* cred_id = first 8 bytes of SHA-256(cred_pub[65]). Stable id for a credential
 * public key so the verifier can bind presence to a specific enrolled
 * credential. */
void aliro_assert_cred_id(const uint8_t cred_pub[ALIRO_ASSERT_PUB_LEN],
			  uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN]);

/*
 * Signing seam for the ECDSA-P256 mode.
 *
 * The curve arithmetic deliberately does NOT live in this module. Keeping the
 * codec free of any crypto-backend dependency is what lets it stay portable
 * C11, model-checkable and fuzzable, and it lets each caller bind whatever
 * P-256 it already has -- on target that is aliro_ecdsa_p256_sign/verify from
 * aliro_prim.h, in tests it is a double that can assert exactly which bytes
 * were presented for signature.
 *
 * Both take the signed prefix as msg (never a pre-hash: the backend hashes
 * internally, matching aliro_prim's ECDSA-P256-SHA256 contract) and return 0
 * on success, non-zero on failure. sig is r||s, 32 bytes each.
 */
typedef int (*aliro_assert_sign_fn)(void *ctx, const uint8_t *msg, size_t msg_len,
				    uint8_t sig[ALIRO_ASSERT_SIG_LEN]);
typedef int (*aliro_assert_verify_fn)(void *ctx, const uint8_t *msg, size_t msg_len,
				      const uint8_t sig[ALIRO_ASSERT_SIG_LEN]);

/* Serialise + HMAC an assertion into an ALIRO_ASSERT_WIRE_HMAC-byte frame under
 * key. Returns 0 and sets *wire_len; -1 if wire_cap is too small. */
int aliro_assert_build(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const struct aliro_assert *a,
		       uint8_t *wire, size_t wire_cap, size_t *wire_len);

/* Serialise an assertion and have sign() sign it, producing an
 * ALIRO_ASSERT_WIRE_P256-byte frame. Returns 0 and sets *wire_len; -1 if
 * wire_cap is too small, sign is NULL, or sign() fails. */
int aliro_assert_build_p256(aliro_assert_sign_fn sign, void *ctx, const struct aliro_assert *a,
			    uint8_t *wire, size_t wire_cap, size_t *wire_len);

/*
 * Parse + fully verify a P-256 frame. Identical to aliro_assert_verify except
 * that authentication is delegated to verify(); every later check, and the
 * order they run in, is shared with the HMAC path. A failed signature reports
 * ALIRO_ASSERT_E_MAC, the same as a failed HMAC -- from the caller's side both
 * mean "this frame is not authentic".
 */
int aliro_assert_verify_p256(aliro_assert_verify_fn verify, void *ctx, const uint8_t *wire,
			     size_t wire_len, const uint8_t expected_nonce[ALIRO_ASSERT_NONCE_LEN],
			     uint16_t threshold_cm, uint64_t min_uptime_ms,
			     struct aliro_assert *out);

/*
 * Parse + fully verify an HMAC frame. Checks, in order: length/magic/version,
 * alg == HMAC-SHA256, HMAC (constant-time), nonce echo, forward-progress
 * (uptime_ms > min_uptime_ms; pass 0 to skip), status == PRESENT, distance_cm
 * <= threshold_cm.
 *
 * Returns ALIRO_ASSERT_OK (0) only when all pass; otherwise the first failing
 * reason. *out, when non-NULL, is populated with whatever parsed AFTER the tag
 * check passed (for logging / cred-allowlist); it is left untouched if the tag
 * or framing is bad, and must never be trusted for an unlock on a non-zero
 * return. threshold_cm is inclusive.
 */
int aliro_assert_verify(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const uint8_t *wire,
			size_t wire_len, const uint8_t expected_nonce[ALIRO_ASSERT_NONCE_LEN],
			uint16_t threshold_cm, uint64_t min_uptime_ms, struct aliro_assert *out);

#ifdef __cplusplus
}
#endif
