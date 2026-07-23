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

/* Fixed frame layout (see the header). Offsets of each field within the frame. */
#define OFF_MAGIC    0u
#define OFF_VERSION  2u
#define OFF_STATUS   3u
#define OFF_NONCE    4u
#define OFF_CREDID   20u
#define OFF_DISTANCE 28u
#define OFF_UPTIME   30u
#define OFF_MAC      38u /* == number of MAC'd bytes */

#define ASSERT_MAGIC0  0xA1u
#define ASSERT_MAGIC1  0x50u
#define ASSERT_VERSION 0x01u

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

void aliro_assert_cred_id(const uint8_t cred_pub[65], uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN])
{
	uint8_t digest[ALIRO_SHA256_LEN];

	aliro_sha256(cred_pub, 65u, digest);
	memcpy(cred_id, digest, ALIRO_ASSERT_CREDID_LEN);
}

int aliro_assert_build(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const struct aliro_assert *a,
		       uint8_t *wire, size_t wire_cap, size_t *wire_len)
{
	if (wire == NULL || a == NULL || wire_cap < ALIRO_ASSERT_WIRE_LEN) {
		return -1;
	}

	wire[OFF_MAGIC] = ASSERT_MAGIC0;
	wire[OFF_MAGIC + 1u] = ASSERT_MAGIC1;
	wire[OFF_VERSION] = ASSERT_VERSION;
	wire[OFF_STATUS] = a->status;
	memcpy(wire + OFF_NONCE, a->nonce, ALIRO_ASSERT_NONCE_LEN);
	memcpy(wire + OFF_CREDID, a->cred_id, ALIRO_ASSERT_CREDID_LEN);
	put_be16(wire + OFF_DISTANCE, a->distance_cm);
	put_be64(wire + OFF_UPTIME, a->uptime_ms);

	/* MAC over every byte before the tag. */
	aliro_hmac_sha256(key, ALIRO_ASSERT_KEY_LEN, wire, OFF_MAC, wire + OFF_MAC);

	if (wire_len != NULL) {
		*wire_len = ALIRO_ASSERT_WIRE_LEN;
	}
	return 0;
}

int aliro_assert_verify(const uint8_t key[ALIRO_ASSERT_KEY_LEN], const uint8_t *wire,
			size_t wire_len, const uint8_t expected_nonce[ALIRO_ASSERT_NONCE_LEN],
			uint16_t threshold_cm, uint64_t min_uptime_ms, struct aliro_assert *out)
{
	if (wire == NULL || wire_len != ALIRO_ASSERT_WIRE_LEN || wire[OFF_MAGIC] != ASSERT_MAGIC0 ||
	    wire[OFF_MAGIC + 1u] != ASSERT_MAGIC1 || wire[OFF_VERSION] != ASSERT_VERSION) {
		return ALIRO_ASSERT_E_MALFORMED;
	}

	/* Authenticate before interpreting any field. */
	uint8_t mac[ALIRO_ASSERT_MAC_LEN];

	aliro_hmac_sha256(key, ALIRO_ASSERT_KEY_LEN, wire, OFF_MAC, mac);
	if (!ct_equal(mac, wire + OFF_MAC, ALIRO_ASSERT_MAC_LEN)) {
		return ALIRO_ASSERT_E_MAC;
	}

	/* Authentic: parse for the caller (logging / cred-allowlist) even on a
	 * later semantic reject, so the reason can be recorded. */
	struct aliro_assert a;

	a.status = wire[OFF_STATUS];
	memcpy(a.nonce, wire + OFF_NONCE, ALIRO_ASSERT_NONCE_LEN);
	memcpy(a.cred_id, wire + OFF_CREDID, ALIRO_ASSERT_CREDID_LEN);
	a.distance_cm = get_be16(wire + OFF_DISTANCE);
	a.uptime_ms = get_be64(wire + OFF_UPTIME);
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
