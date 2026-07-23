/** @file test_aliro_assert.c — presence-assertion codec + verifier.
 *
 * The security core of the non-door primitive: a spoofed USB device must not be
 * able to assert presence. Covers the happy path and every distinct reject:
 * wrong key (forged MAC), replayed/other nonce, stale uptime, absent status,
 * out-of-range distance, and malformed framing.
 */
#include <string.h>

#include "aliro_assert.h"
#include "test.h"

/* A fixed pairing key + a built assertion the tests mutate. */
static const uint8_t k_key[ALIRO_ASSERT_KEY_LEN] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
	0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a,
	0x69, 0x78, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
static const uint8_t k_nonce[ALIRO_ASSERT_NONCE_LEN] = {
	0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04,
	0xa5, 0xa5, 0x5a, 0x5a, 0x10, 0x20, 0x30, 0x40,
};

static void base_assertion(struct aliro_assert *a)
{
	memset(a, 0, sizeof(*a));
	a->status = ALIRO_PRESENCE_PRESENT;
	memcpy(a->nonce, k_nonce, ALIRO_ASSERT_NONCE_LEN);
	for (unsigned i = 0; i < ALIRO_ASSERT_CREDID_LEN; i++) {
		a->cred_id[i] = (uint8_t)(0xC0u + i);
	}
	a->distance_cm = 25;
	a->uptime_ms = 1000000;
}

void test_aliro_assert(void)
{
	struct aliro_assert a;
	base_assertion(&a);
	uint8_t wire[ALIRO_ASSERT_WIRE_LEN];
	size_t wlen = 0;

	t_group("build");
	T_EQ("build.rc", aliro_assert_build(k_key, &a, wire, sizeof(wire), &wlen), 0);
	T_EQ("build.len", (long)wlen, ALIRO_ASSERT_WIRE_LEN);
	T_EQ("build.magic0", wire[0], 0xA1);
	T_EQ("build.magic1", wire[1], 0x50);
	T_EQ("build.version", wire[2], 0x01);
	uint8_t tiny[8];
	T_EQ("build.too_small", aliro_assert_build(k_key, &a, tiny, sizeof(tiny), &wlen), -1);
	/* Lock the whole 70-byte frame so the wire format + MAC cannot drift.
	 * magic|ver|status|nonce|cred_id|dist=0019(25)|uptime=0f4240(1e6)|HMAC. */
	t_vec("build.frame", wire, sizeof(wire),
	      "a1500101deadbeef01020304a5a55a5a10203040c0c1c2c3c4c5c6c700190000"
	      "0000000f4240b3533a5bc0f740685f3330ad30afe93fbeaf8bd409c74c4367f6"
	      "ae8de25ff4fb");

	t_group("verify OK");
	struct aliro_assert out;
	memset(&out, 0, sizeof(out));
	T_EQ("ok", aliro_assert_verify(k_key, wire, wlen, k_nonce, 40, 0, &out), ALIRO_ASSERT_OK);
	T_EQ("ok.dist", out.distance_cm, 25);
	T_EQ("ok.status", out.status, ALIRO_PRESENCE_PRESENT);
	T_OK("ok.credid", memcmp(out.cred_id, a.cred_id, ALIRO_ASSERT_CREDID_LEN) == 0);
	/* Threshold is inclusive: exactly at the boundary passes. */
	T_EQ("ok.boundary", aliro_assert_verify(k_key, wire, wlen, k_nonce, 25, 0, &out),
	     ALIRO_ASSERT_OK);
	/* Forward-progress guard: uptime strictly greater than the floor passes. */
	T_EQ("ok.fresh", aliro_assert_verify(k_key, wire, wlen, k_nonce, 40, 999999, &out),
	     ALIRO_ASSERT_OK);

	t_group("reject: wrong key (forged MAC)");
	uint8_t badkey[ALIRO_ASSERT_KEY_LEN];
	memcpy(badkey, k_key, sizeof(badkey));
	badkey[0] ^= 0x01;
	T_EQ("wrong_key", aliro_assert_verify(badkey, wire, wlen, k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_MAC);
	/* Any single-bit tamper of the MAC'd region also fails the MAC. */
	uint8_t tampered[ALIRO_ASSERT_WIRE_LEN];
	memcpy(tampered, wire, sizeof(tampered));
	tampered[28] ^= 0x08; /* flip distance high byte */
	T_EQ("tamper", aliro_assert_verify(k_key, tampered, sizeof(tampered), k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_MAC);

	t_group("reject: replay / nonce mismatch");
	uint8_t other_nonce[ALIRO_ASSERT_NONCE_LEN];
	memcpy(other_nonce, k_nonce, sizeof(other_nonce));
	other_nonce[15] ^= 0xFF;
	T_EQ("nonce", aliro_assert_verify(k_key, wire, wlen, other_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_NONCE);

	t_group("reject: stale (uptime not advancing)");
	/* uptime_ms == min is stale; a replayed frame at the same uptime is rejected. */
	T_EQ("stale.eq", aliro_assert_verify(k_key, wire, wlen, k_nonce, 40, 1000000, &out),
	     ALIRO_ASSERT_E_STALE);
	T_EQ("stale.lt", aliro_assert_verify(k_key, wire, wlen, k_nonce, 40, 1000001, &out),
	     ALIRO_ASSERT_E_STALE);

	t_group("reject: absent status");
	struct aliro_assert absent = a;
	absent.status = ALIRO_PRESENCE_ABSENT;
	uint8_t awire[ALIRO_ASSERT_WIRE_LEN];
	aliro_assert_build(k_key, &absent, awire, sizeof(awire), NULL);
	T_EQ("absent", aliro_assert_verify(k_key, awire, sizeof(awire), k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_ABSENT);

	t_group("reject: out of range / no range");
	struct aliro_assert far = a;
	far.distance_cm = 41;
	uint8_t fwire[ALIRO_ASSERT_WIRE_LEN];
	aliro_assert_build(k_key, &far, fwire, sizeof(fwire), NULL);
	T_EQ("far", aliro_assert_verify(k_key, fwire, sizeof(fwire), k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_RANGE);
	struct aliro_assert norange = a;
	norange.distance_cm = ALIRO_ASSERT_DIST_NONE;
	uint8_t nwire[ALIRO_ASSERT_WIRE_LEN];
	aliro_assert_build(k_key, &norange, nwire, sizeof(nwire), NULL);
	T_EQ("no_range", aliro_assert_verify(k_key, nwire, sizeof(nwire), k_nonce, 0xFFFE, 0, &out),
	     ALIRO_ASSERT_E_RANGE);

	t_group("reject: malformed framing");
	T_EQ("short_len", aliro_assert_verify(k_key, wire, wlen - 1u, k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	uint8_t bad[ALIRO_ASSERT_WIRE_LEN];
	memcpy(bad, wire, sizeof(bad));
	bad[0] = 0x00; /* bad magic */
	T_EQ("bad_magic", aliro_assert_verify(k_key, bad, sizeof(bad), k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	memcpy(bad, wire, sizeof(bad));
	bad[2] = 0x02; /* bad version */
	T_EQ("bad_version", aliro_assert_verify(k_key, bad, sizeof(bad), k_nonce, 40, 0, &out),
	     ALIRO_ASSERT_E_MALFORMED);

	t_group("cred_id derivation");
	uint8_t pub[65];
	memset(pub, 0, sizeof(pub));
	pub[0] = 0x04;
	pub[1] = 0xAB;
	uint8_t cid[ALIRO_ASSERT_CREDID_LEN];
	aliro_assert_cred_id(pub, cid);
	/* Stable: first 8 bytes of SHA-256(pub). Locked value. */
	t_vec("cred_id", cid, sizeof(cid), "279125357c1cad0f");
}
