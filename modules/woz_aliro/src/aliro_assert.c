// Presence-assertion wire codec + verifier (see aliro_assert.h). Serialises a
// dongle's "credential present within N cm for this nonce" statement, MACs it with
// HMAC-SHA256 under the paired key, and verifies an incoming frame against a
// challenge nonce + distance threshold. Portable C11; no UWB/BLE/platform deps.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>

#include "aliro_assert.h"
#include "aliro_hash.h"

/* Frame layout (see the header). Offsets of each field within the frame. The
 * signed prefix is the same for every algorithm; only the trailing tag differs
 * in length, so OFF_TAG == ALIRO_ASSERT_SIGNED_LEN == the number of signed
 * bytes. */
#define OFF_MAGIC    0u
#define OFF_VERSION  2u
#define OFF_ALG      3u
#define OFF_STATUS   4u
#define OFF_NONCE    5u
#define OFF_CREDID   21u
#define OFF_DISTANCE 29u
#define OFF_UPTIME   31u
#define OFF_UNIX     39u
#define OFF_TAG      ALIRO_ASSERT_SIGNED_LEN /* == number of signed bytes */

#define ASSERT_MAGIC0  0xA1u
#define ASSERT_MAGIC1  0x50u
#define ASSERT_VERSION 0x02u

// Writes v as 2 big-endian bytes to p.
static void put_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

// Reads 2 big-endian bytes from p.
static uint16_t get_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// Writes v as 8 big-endian bytes to p.
static void put_be64(uint8_t *p, uint64_t v)
{
	for (int i = 7; i >= 0; i--) {
		p[i] = (uint8_t)v;
		v >>= 8;
	}
}

// Reads 8 big-endian bytes from p.
static uint64_t get_be64(const uint8_t *p)
{
	uint64_t v = 0;

	for (unsigned i = 0; i < 8u; i++) {
		v = (v << 8) | p[i];
	}
	return v;
}

// Constant-time equality of two n-byte buffers: 1 if equal, 0 otherwise. Timing
// is independent of where the first differing byte is, so a MAC check does not
// leak how much of a forged tag was correct.
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint8_t diff = 0;

	for (size_t i = 0; i < n; i++) {
		diff |= (uint8_t)(a[i] ^ b[i]);
	}
	return diff == 0;
}

size_t aliro_assert_wire_len(uint8_t alg)
{
	switch (alg) {
	case ALIRO_ASSERT_ALG_ECDSA_P256:
		return ALIRO_ASSERT_WIRE_P256;
	default:
		return 0u;
	}
}

uint8_t aliro_assert_peek_alg(const uint8_t *buf, size_t len)
{
	if (buf == NULL || len < ALIRO_ASSERT_SIGNED_LEN) {
		return 0u;
	}
	return buf[OFF_ALG];
}

void aliro_assert_cred_id(const uint8_t cred_pub[ALIRO_ASSERT_PUB_LEN],
			  uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN])
{
	uint8_t digest[ALIRO_SHA256_LEN];

	aliro_sha256(cred_pub, ALIRO_ASSERT_PUB_LEN, digest);
	memcpy(cred_id, digest, ALIRO_ASSERT_CREDID_LEN);
}

// Writes the signed prefix (everything up to the tag) for one algorithm. Shared
// so the two build paths cannot drift into producing different prefixes.
static void put_prefix(uint8_t *wire, uint8_t alg, const struct aliro_assert *a)
{
	wire[OFF_MAGIC] = ASSERT_MAGIC0;
	wire[OFF_MAGIC + 1u] = ASSERT_MAGIC1;
	wire[OFF_VERSION] = ASSERT_VERSION;
	wire[OFF_ALG] = alg;
	wire[OFF_STATUS] = a->status;
	memcpy(wire + OFF_NONCE, a->nonce, ALIRO_ASSERT_NONCE_LEN);
	memcpy(wire + OFF_CREDID, a->cred_id, ALIRO_ASSERT_CREDID_LEN);
	put_be16(wire + OFF_DISTANCE, a->distance_cm);
	put_be64(wire + OFF_UPTIME, a->uptime_ms);
	put_be64(wire + OFF_UNIX, a->unix_ms);
}

/*
 * Framing checks common to both verifiers: length for the algorithm actually
 * named in the frame, magic, version, and that the algorithm is the one this
 * verifier implements. Returns ALIRO_ASSERT_OK when the frame is safe to
 * authenticate.
 */
static int check_framing(const uint8_t *wire, size_t wire_len, uint8_t want_alg)
{
	/* Enough bytes to hold the header fields this function reads. */
	if (wire == NULL || wire_len < ALIRO_ASSERT_SIGNED_LEN) {
		return ALIRO_ASSERT_E_MALFORMED;
	}
	if (wire[OFF_MAGIC] != ASSERT_MAGIC0 || wire[OFF_MAGIC + 1u] != ASSERT_MAGIC1 ||
	    wire[OFF_VERSION] != ASSERT_VERSION) {
		return ALIRO_ASSERT_E_MALFORMED;
	}
	/* Algorithm before length, deliberately. A well-formed frame for the other
	 * algorithm should be reported as the wrong ALGORITHM whatever its length,
	 * rather than as generic malformed framing -- the two have very different
	 * causes and only one of them means "your peer is misconfigured". */
	if (wire[OFF_ALG] != want_alg) {
		return ALIRO_ASSERT_E_ALG;
	}
	if (wire_len != aliro_assert_wire_len(want_alg)) {
		return ALIRO_ASSERT_E_MALFORMED;
	}
	return ALIRO_ASSERT_OK;
}

/*
 * Everything after authentication: parse the fields, hand them to the caller
 * for logging even on a semantic reject, then apply the policy checks in a
 * fixed order. Shared by both verifiers so the two can never disagree about
 * what an authentic frame means.
 */
static int parse_and_check(const uint8_t *wire, const uint8_t *expected_nonce,
			   uint16_t threshold_cm, uint64_t min_uptime_ms, struct aliro_assert *out)
{
	struct aliro_assert a;

	a.status = wire[OFF_STATUS];
	memcpy(a.nonce, wire + OFF_NONCE, ALIRO_ASSERT_NONCE_LEN);
	memcpy(a.cred_id, wire + OFF_CREDID, ALIRO_ASSERT_CREDID_LEN);
	a.distance_cm = get_be16(wire + OFF_DISTANCE);
	a.uptime_ms = get_be64(wire + OFF_UPTIME);
	a.unix_ms = get_be64(wire + OFF_UNIX);
	if (out != NULL) {
		*out = a;
	}

	if (expected_nonce == NULL || !ct_equal(a.nonce, expected_nonce, ALIRO_ASSERT_NONCE_LEN)) {
		return ALIRO_ASSERT_E_NONCE;
	}
	if (min_uptime_ms != 0u && a.uptime_ms <= min_uptime_ms) {
		return ALIRO_ASSERT_E_STALE;
	}
	if (a.status != ALIRO_PRESENCE_PRESENT) {
		return ALIRO_ASSERT_E_ABSENT;
	}
	if (a.distance_cm == ALIRO_ASSERT_DIST_NONE || a.distance_cm > threshold_cm) {
		return ALIRO_ASSERT_E_RANGE;
	}
	return ALIRO_ASSERT_OK;
}

int aliro_assert_build_p256(aliro_assert_sign_fn sign, void *ctx, const struct aliro_assert *a,
			    uint8_t *wire, size_t wire_cap, size_t *wire_len)
{
	if (sign == NULL || wire == NULL || a == NULL || wire_cap < ALIRO_ASSERT_WIRE_P256) {
		return -1;
	}

	put_prefix(wire, ALIRO_ASSERT_ALG_ECDSA_P256, a);

	/* Sign every byte before the tag: magic, version, alg and all the claimed
	 * facts, so none of them can be edited without breaking the signature. */
	if (sign(ctx, wire, OFF_TAG, wire + OFF_TAG) != 0) {
		return -1;
	}

	if (wire_len != NULL) {
		*wire_len = ALIRO_ASSERT_WIRE_P256;
	}
	return 0;
}

int aliro_assert_verify_p256(aliro_assert_verify_fn verify, void *ctx, const uint8_t *wire,
			     size_t wire_len, const uint8_t expected_nonce[ALIRO_ASSERT_NONCE_LEN],
			     uint16_t threshold_cm, uint64_t min_uptime_ms,
			     struct aliro_assert *out)
{
	if (verify == NULL) {
		return ALIRO_ASSERT_E_MALFORMED;
	}

	int fr = check_framing(wire, wire_len, ALIRO_ASSERT_ALG_ECDSA_P256);

	if (fr != ALIRO_ASSERT_OK) {
		return fr;
	}

	/* Authenticate before interpreting any field. A backend that fails for any
	 * reason -- bad signature, malformed point, backend error -- is a frame we
	 * cannot trust, so all of it collapses to one verdict. */
	if (verify(ctx, wire, OFF_TAG, wire + OFF_TAG) != 0) {
		return ALIRO_ASSERT_E_MAC;
	}

	return parse_and_check(wire, expected_nonce, threshold_cm, min_uptime_ms, out);
}
