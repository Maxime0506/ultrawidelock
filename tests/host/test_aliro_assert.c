/** @file test_aliro_assert.c — presence-assertion codec + verifier.
 *
 * The security core of the non-door primitive: a spoofed USB device must not be
 * able to assert presence. Covers the happy path and every distinct reject:
 * forged or tampered signature, replayed/other nonce, stale uptime, absent
 * status, out-of-range distance, and malformed framing.
 */
#include <string.h>

#include "aliro_assert.h"
#include "aliro_hash.h"
#include "test.h"

/*
 * Stand-in for the P-256 backend.
 *
 * This is NOT ECDSA and proves nothing about curve arithmetic -- the host suite
 * has no real P-256 (the PSA seam is a recording fake), and real signature
 * validation belongs in an on-target test. What it does prove is everything the
 * codec is actually responsible for: that the bytes handed to the backend are
 * exactly the signed prefix, that a tampered frame fails authentication, and
 * that authentication happens before any field is trusted. sig is a
 * deterministic function of the message, so tampering anywhere in the prefix
 * changes it.
 */
struct fake_ec {
	unsigned sign_calls;
	unsigned verify_calls;
	size_t last_msg_len;
	uint8_t last_msg[ALIRO_ASSERT_WIRE_MAX];
	int force_fail;
};

static void fake_ec_tag(const uint8_t *msg, size_t msg_len, uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	uint8_t d[ALIRO_SHA256_LEN];

	aliro_sha256(msg, msg_len, d);
	memcpy(sig, d, ALIRO_SHA256_LEN);
	aliro_sha256(d, sizeof(d), sig + ALIRO_SHA256_LEN);
}

static int fake_ec_sign(void *ctx, const uint8_t *msg, size_t msg_len,
			uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	struct fake_ec *f = ctx;

	f->sign_calls++;
	f->last_msg_len = msg_len;
	memcpy(f->last_msg, msg, msg_len);
	if (f->force_fail) {
		return -1;
	}
	fake_ec_tag(msg, msg_len, sig);
	return 0;
}

static int fake_ec_verify(void *ctx, const uint8_t *msg, size_t msg_len,
			  const uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	struct fake_ec *f = ctx;
	uint8_t want[ALIRO_ASSERT_SIG_LEN];

	f->verify_calls++;
	f->last_msg_len = msg_len;
	memcpy(f->last_msg, msg, msg_len);
	if (f->force_fail) {
		return -1;
	}
	fake_ec_tag(msg, msg_len, want);
	return memcmp(want, sig, sizeof(want)) == 0 ? 0 : -1;
}

/* A fixed challenge nonce + a built assertion the tests mutate. */
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
	/* Non-zero so the byte-exact vector below actually pins where unix_ms
	 * sits; all-zero would pass even if the field were misplaced. */
	a->unix_ms = 1785000000000ULL;
}

void test_aliro_assert(void)
{
	struct fake_ec fec;
	memset(&fec, 0, sizeof(fec));
	struct aliro_assert a;
	base_assertion(&a);
	uint8_t wire[ALIRO_ASSERT_WIRE_P256];
	size_t wlen = 0;

	t_group("build");
	T_EQ("build.rc",
	     aliro_assert_build_p256(fake_ec_sign, &fec, &a, wire, sizeof(wire), &wlen), 0);
	T_EQ("build.len", (long)wlen, ALIRO_ASSERT_WIRE_P256);
	T_EQ("build.magic0", wire[0], 0xA1);
	T_EQ("build.magic1", wire[1], 0x50);
	T_EQ("build.version", wire[2], 0x02);
	T_EQ("build.alg", wire[3], ALIRO_ASSERT_ALG_ECDSA_P256);
	uint8_t tiny[8];
	T_EQ("build.too_small",
	     aliro_assert_build_p256(fake_ec_sign, &fec, &a, tiny, sizeof(tiny), &wlen), -1);
	/* Lock the 47-byte signed prefix so the wire layout cannot drift. Derived
	 * independently (Python struct) before being pinned here. The signature is
	 * not pinned: it comes from the test double, so it would pin the double
	 * rather than the format.
	 * magic|ver=02|alg=02|status|nonce|cred_id|dist=0019(25)|
	 * uptime=0f4240(1e6)|unix=019f9a4a7a00(1785000000000). */
	t_vec("build.prefix", wire, ALIRO_ASSERT_SIGNED_LEN,
	      "a150020201deadbeef01020304a5a55a5a10203040c0c1c2c3c4c5c6c700190000"
	      "0000000f42400000019f9a4a7a00");
	/* The backend must be handed the signed prefix -- all of it, and nothing
	 * but it. A backend fed the wrong span would still round-trip against
	 * itself, so this is checked directly rather than inferred. */
	T_EQ("build.signed_len", (long)fec.last_msg_len, ALIRO_ASSERT_SIGNED_LEN);
	T_OK("build.signed_bytes", memcmp(fec.last_msg, wire, ALIRO_ASSERT_SIGNED_LEN) == 0);

	t_group("verify OK");
	struct aliro_assert out;
	memset(&out, 0, sizeof(out));
	T_EQ("ok", aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id,
					    40, 0, &out),
	     ALIRO_ASSERT_OK);
	T_EQ("ok.dist", out.distance_cm, 25);
	T_EQ("ok.status", out.status, ALIRO_PRESENCE_PRESENT);
	T_OK("ok.credid", memcmp(out.cred_id, a.cred_id, ALIRO_ASSERT_CREDID_LEN) == 0);
	/* Decode side of the two 64-bit clocks: the prefix KAT above pins how they
	 * are written, this pins that verify hands them back. */
	T_OK("ok.uptime", out.uptime_ms == 1000000ULL);
	T_OK("ok.unix", out.unix_ms == 1785000000000ULL);
	/* Threshold is inclusive: exactly at the boundary passes. */
	T_EQ("ok.boundary",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id, 25, 0,
				      &out),
	     ALIRO_ASSERT_OK);
	/* Forward-progress guard: uptime strictly greater than the floor passes. */
	T_EQ("ok.fresh",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id, 40,
				      999999, &out),
	     ALIRO_ASSERT_OK);

	t_group("reject: tampered / unauthentic");
	/* Any single-bit tamper of the signed region fails authentication. */
	uint8_t tampered[ALIRO_ASSERT_WIRE_P256];
	memcpy(tampered, wire, sizeof(tampered));
	tampered[29] ^= 0x08; /* flip distance high byte: claim a different distance */
	T_EQ("tamper.prefix",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, tampered, sizeof(tampered), k_nonce,
				      a.cred_id, 40, 0, &out),
	     ALIRO_ASSERT_E_MAC);
	memcpy(tampered, wire, sizeof(tampered));
	tampered[ALIRO_ASSERT_SIGNED_LEN] ^= 0x01; /* tamper the signature itself */
	T_EQ("tamper.sig",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, tampered, sizeof(tampered), k_nonce,
				      a.cred_id, 40, 0, &out),
	     ALIRO_ASSERT_E_MAC);
	/* Backend failures propagate as not-authentic, never as a silently accepted
	 * frame -- a backend error must not read as a pass. */
	fec.force_fail = 1;
	T_EQ("backend_fails",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id, 40, 0,
				      &out),
	     ALIRO_ASSERT_E_MAC);
	T_EQ("sign_fails",
	     aliro_assert_build_p256(fake_ec_sign, &fec, &a, tampered, sizeof(tampered), NULL), -1);
	fec.force_fail = 0;

	t_group("reject: replay / nonce mismatch");
	uint8_t other_nonce[ALIRO_ASSERT_NONCE_LEN];
	memcpy(other_nonce, k_nonce, sizeof(other_nonce));
	other_nonce[15] ^= 0xFF;
	T_EQ("nonce",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, other_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_NONCE);

	t_group("reject: credential mismatch");
	uint8_t other_cred_id[ALIRO_ASSERT_CREDID_LEN];
	memcpy(other_cred_id, a.cred_id, sizeof(other_cred_id));
	other_cred_id[0] ^= 0xFF;
	T_EQ("credential",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, other_cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_CREDENTIAL);

	t_group("reject: stale (uptime not advancing)");
	/* uptime_ms == min is stale; a replayed frame at the same uptime is rejected. */
	T_EQ("stale.eq",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id, 40,
				      1000000, &out),
	     ALIRO_ASSERT_E_STALE);
	T_EQ("stale.lt",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen, k_nonce, a.cred_id, 40,
				      1000001, &out),
	     ALIRO_ASSERT_E_STALE);

	t_group("reject: absent status");
	struct aliro_assert absent = a;
	absent.status = ALIRO_PRESENCE_ABSENT;
	uint8_t awire[ALIRO_ASSERT_WIRE_P256];
	aliro_assert_build_p256(fake_ec_sign, &fec, &absent, awire, sizeof(awire), NULL);
	T_EQ("absent",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, awire, sizeof(awire), k_nonce,
				      a.cred_id, 40, 0, &out),
	     ALIRO_ASSERT_E_ABSENT);

	t_group("reject: out of range / no range");
	struct aliro_assert far = a;
	far.distance_cm = 41;
	uint8_t fwire[ALIRO_ASSERT_WIRE_P256];
	aliro_assert_build_p256(fake_ec_sign, &fec, &far, fwire, sizeof(fwire), NULL);
	T_EQ("far",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, fwire, sizeof(fwire), k_nonce,
				      a.cred_id, 40, 0, &out),
	     ALIRO_ASSERT_E_RANGE);
	struct aliro_assert norange = a;
	norange.distance_cm = ALIRO_ASSERT_DIST_NONE;
	uint8_t nwire[ALIRO_ASSERT_WIRE_P256];
	aliro_assert_build_p256(fake_ec_sign, &fec, &norange, nwire, sizeof(nwire), NULL);
	T_EQ("no_range",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, nwire, sizeof(nwire), k_nonce,
				      a.cred_id, 0xFFFE, 0, &out),
	     ALIRO_ASSERT_E_RANGE);

	t_group("reject: malformed framing");
	T_EQ("short_len",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, wire, wlen - 1u, k_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	uint8_t bad[ALIRO_ASSERT_WIRE_P256];
	memcpy(bad, wire, sizeof(bad));
	bad[0] = 0x00; /* bad magic */
	T_EQ("bad_magic",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, bad, sizeof(bad), k_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	memcpy(bad, wire, sizeof(bad));
	bad[2] = 0x03; /* bad version (0x02 is current) */
	T_EQ("bad_version",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, bad, sizeof(bad), k_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	/* A frame naming an algorithm this verifier does not implement must be
	 * refused as a wrong ALGORITHM, not mislabelled as a bad signature. Alg 1 is
	 * the retired HMAC mode: a v1 frame must reject here rather than be
	 * reinterpreted. Rejected before the signature is even checked. */
	memcpy(bad, wire, sizeof(bad));
	bad[3] = 0x01;
	T_EQ("retired_alg",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, bad, sizeof(bad), k_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_ALG);
	memcpy(bad, wire, sizeof(bad));
	bad[3] = 0x7F; /* unknown algorithm */
	T_EQ("unknown_alg",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, bad, sizeof(bad), k_nonce, a.cred_id, 40,
				      0, &out),
	     ALIRO_ASSERT_E_ALG);

	t_group("wire length by algorithm");
	T_EQ("len.p256", (long)aliro_assert_wire_len(ALIRO_ASSERT_ALG_ECDSA_P256),
	     ALIRO_ASSERT_WIRE_P256);
	/* The retired HMAC value has no length: a scanner must not size a buffer
	 * from it, and the value stays reserved rather than being reassigned. */
	T_EQ("len.retired", (long)aliro_assert_wire_len(0x01), 0);
	T_EQ("len.unknown", (long)aliro_assert_wire_len(0x7F), 0);
	T_EQ("peek.alg", aliro_assert_peek_alg(wire, sizeof(wire)), ALIRO_ASSERT_ALG_ECDSA_P256);
	/* Too short to contain the alg byte's own prefix: report invalid, not a
	 * read past the end of the caller's buffer. */
	T_EQ("peek.short", aliro_assert_peek_alg(wire, ALIRO_ASSERT_SIGNED_LEN - 1u), 0);

	/* NULL arguments are caller bugs, but they must fail loudly at the guard
	 * rather than dereference: these are the paths a wrong integration hits. */
	t_group("misuse: NULL arguments");
	T_EQ("null.build_assert",
	     aliro_assert_build_p256(fake_ec_sign, &fec, NULL, wire, sizeof(wire), &wlen), -1);
	T_EQ("null.build_wire",
	     aliro_assert_build_p256(fake_ec_sign, &fec, &a, NULL, ALIRO_ASSERT_WIRE_P256, &wlen),
	     -1);
	T_EQ("null.build_sign",
	     aliro_assert_build_p256(NULL, &fec, &a, wire, sizeof(wire), &wlen), -1);
	T_EQ("null.build_small",
	     aliro_assert_build_p256(fake_ec_sign, &fec, &a, wire, ALIRO_ASSERT_WIRE_P256 - 1u,
				     &wlen),
	     -1);
	T_EQ("null.verify_wire",
	     aliro_assert_verify_p256(fake_ec_verify, &fec, NULL, ALIRO_ASSERT_WIRE_P256, k_nonce,
				      a.cred_id, 40, 0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	T_EQ("null.verify_fn",
	     aliro_assert_verify_p256(NULL, &fec, wire, ALIRO_ASSERT_WIRE_P256, k_nonce, a.cred_id,
				      40, 0, &out),
	     ALIRO_ASSERT_E_MALFORMED);
	T_EQ("null.peek", aliro_assert_peek_alg(NULL, ALIRO_ASSERT_WIRE_MAX), 0);

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
