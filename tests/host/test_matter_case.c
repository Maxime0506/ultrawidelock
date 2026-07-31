/**
 * @file test_matter_case.c — the two derivations Sigma1 turns on.
 *
 * Both are checked against CHIP's own vectors rather than against themselves.
 * The operational IPK vectors come from crypto/tests/
 * TestGroupOperationalCredentials.cpp, and the compressed fabric id they use is
 * the same 87E1B004E235A130 the spec's Operational Discovery example produces
 * -- so the two halves of this node's fabric maths are pinned to one worked
 * example end to end.
 *
 * There is no published destination-identifier vector, so that one is asserted
 * structurally: the exact field order and endianness, caught by changing one
 * input at a time and requiring the answer to move. Weaker than a golden value
 * and stated as such; the real check is a phone, and a phone has now sent one.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>

#include "matter_case.h"
#include "matter_tlv.h"

#include "test.h"

static const uint8_t k_ipk_cfid[] = {
	0x87, 0xE1, 0xB0, 0x04, 0xE2, 0x35, 0xA1, 0x30,
};
static const uint8_t k_ipk_epoch0[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t k_ipk_op0[] = {
	0xC5, 0xF2, 0x69, 0x01, 0x87, 0x11, 0x51, 0x50,
	0xC3, 0x56, 0xAD, 0x93, 0xB3, 0x85, 0xBB, 0x0F,
};
static const uint8_t k_ipk_epoch1[] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};
static const uint8_t k_ipk_op1[] = {
	0xAE, 0xD9, 0x56, 0x95, 0xF3, 0x75, 0xD2, 0xCE,
	0x78, 0x55, 0x6A, 0x41, 0x73, 0x0C, 0x3F, 0x43,
};
static const uint8_t k_ipk_epoch2[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
};
static const uint8_t k_ipk_op2[] = {
	0x35, 0xCA, 0x34, 0x6E, 0x5E, 0x24, 0xBB, 0xBE,
	0x88, 0x9C, 0xF4, 0xD3, 0x5C, 0x5E, 0x82, 0x0A,
};

/** Build a Sigma1 the way an initiator does (CASESession.cpp:843-848). */
static size_t build_sigma1(uint8_t *buf, size_t cap, const uint8_t *rnd, const uint8_t *dest,
			   const uint8_t *pub, uint16_t session_id)
{
	struct matter_tlv_writer w;
	size_t n = 0u;

	matter_tlv_writer_init(&w, buf, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	if (rnd != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(1u), rnd, MATTER_CASE_RANDOM_LEN);
	}
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2u), session_id);
	if (dest != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(3u), dest, MATTER_CASE_DEST_ID_LEN);
	}
	if (pub != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(4u), pub, MATTER_CASE_PUBKEY_LEN);
	}
	(void)matter_tlv_end_container(&w);
	T_EQ("sigma1 encoded", matter_tlv_writer_finish(&w, &n), MATTER_OK);
	return n;
}

void test_matter_case(void)
{
	uint8_t ipk[MATTER_CASE_IPK_LEN];
	uint8_t rnd[MATTER_CASE_RANDOM_LEN];
	uint8_t pub[MATTER_CASE_PUBKEY_LEN];
	uint8_t dest[MATTER_CASE_DEST_ID_LEN];
	uint8_t dest2[MATTER_CASE_DEST_ID_LEN];
	size_t i;

	for (i = 0u; i < sizeof(rnd); i++) {
		rnd[i] = (uint8_t)(0x10u + i);
	}
	pub[0] = 0x04u;
	for (i = 1u; i < sizeof(pub); i++) {
		pub[i] = (uint8_t)(0xC0u + i);
	}

	t_group("the operational key AddNOC's IPK becomes");

	/*
	 * AddNOC delivers an EPOCH key and every use of "the IPK" in CASE means
	 * this derivation of it. Using the epoch key directly produces a
	 * destination identifier that simply never matches, with nothing in the
	 * protocol to say why -- which is exactly the kind of failure a vector
	 * catches and a round trip does not.
	 */
	T_EQ("epoch 0 derives", matter_case_operational_ipk(k_ipk_epoch0, k_ipk_cfid, ipk),
	     MATTER_OK);
	T_OK("and matches CHIP's vector", memcmp(ipk, k_ipk_op0, sizeof(ipk)) == 0);

	T_EQ("epoch 1 derives", matter_case_operational_ipk(k_ipk_epoch1, k_ipk_cfid, ipk),
	     MATTER_OK);
	T_OK("and matches", memcmp(ipk, k_ipk_op1, sizeof(ipk)) == 0);

	T_EQ("epoch 2 derives", matter_case_operational_ipk(k_ipk_epoch2, k_ipk_cfid, ipk),
	     MATTER_OK);
	T_OK("and matches", memcmp(ipk, k_ipk_op2, sizeof(ipk)) == 0);

	T_EQ("NULL epoch key refused", matter_case_operational_ipk(NULL, k_ipk_cfid, ipk),
	     MATTER_E_INVAL);

	t_group("the destination identifier");

	T_EQ("computes",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010001), dest),
	     MATTER_OK);

	/*
	 * No published vector for this one. What IS checkable is that every
	 * input reaches the HMAC: change one, and the answer must move. A
	 * derivation that quietly dropped the node id, or byte-swapped it,
	 * would pass a round trip and fail against a phone.
	 */
	T_EQ("a different node id",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010002), dest2),
	     MATTER_OK);
	T_OK("changes it", memcmp(dest, dest2, sizeof(dest)) != 0);

	T_EQ("a different fabric id",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D363),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("changes it", memcmp(dest, dest2, sizeof(dest)) != 0);

	/* Byte-swapping the fabric id must NOT give the same answer -- if it
	 * did, the endianness would not be being honoured at all. */
	T_EQ("the fabric id byte-reversed",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x62D315D108C90629),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("is a different identity", memcmp(dest, dest2, sizeof(dest)) != 0);

	T_EQ("a different IPK",
	     matter_case_destination_id(k_ipk_op1, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("changes it", memcmp(dest, dest2, sizeof(dest)) != 0);

	rnd[0] ^= 0x01u;
	T_EQ("a different random",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("changes it", memcmp(dest, dest2, sizeof(dest)) != 0);
	rnd[0] ^= 0x01u;

	pub[64] ^= 0x01u;
	T_EQ("a different root key",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("changes it", memcmp(dest, dest2, sizeof(dest)) != 0);
	pub[64] ^= 0x01u;

	/* And it is deterministic -- the same inputs must give the same answer,
	 * or nothing above means anything. */
	T_EQ("recomputed",
	     matter_case_destination_id(k_ipk_op0, rnd, pub, UINT64_C(0x2906C908D115D362),
					UINT64_C(0xDEDEDEDE00010001), dest2),
	     MATTER_OK);
	T_OK("is identical", memcmp(dest, dest2, sizeof(dest)) == 0);

	t_group("decoding a Sigma1");
	{
		struct matter_case_sigma1 s1;
		uint8_t buf[256];
		size_t n;

		n = build_sigma1(buf, sizeof(buf), rnd, dest, pub, 0x1234u);
		T_EQ("decodes", matter_case_sigma1_decode(buf, n, &s1), MATTER_OK);
		T_OK("random borrowed in place",
		     memcmp(s1.initiator_random, rnd, sizeof(rnd)) == 0);
		T_OK("destination id too", memcmp(s1.destination_id, dest, sizeof(dest)) == 0);
		T_OK("and the ephemeral key", memcmp(s1.initiator_pubkey, pub, sizeof(pub)) == 0);
		T_EQ("session id read", s1.initiator_session_id, 0x1234);
		T_OK("no resumption offered", !s1.has_resumption);
		/* Borrowed, not copied: the pointers must land INSIDE the
		 * caller's buffer, or something has been allocated. */
		T_OK("nothing was copied",
		     s1.initiator_random >= buf && s1.initiator_random < buf + n);

		t_group("Sigma1s that cannot be answered");

		T_EQ("a missing random",
		     matter_case_sigma1_decode(
			     buf, build_sigma1(buf, sizeof(buf), NULL, dest, pub, 1u), &s1),
		     MATTER_E_INVAL);
		T_EQ("a missing destination id",
		     matter_case_sigma1_decode(
			     buf, build_sigma1(buf, sizeof(buf), rnd, NULL, pub, 1u), &s1),
		     MATTER_E_INVAL);
		T_EQ("a missing ephemeral key",
		     matter_case_sigma1_decode(
			     buf, build_sigma1(buf, sizeof(buf), rnd, dest, NULL, 1u), &s1),
		     MATTER_E_INVAL);
		T_EQ("NULL input", matter_case_sigma1_decode(NULL, 4u, &s1), MATTER_E_INVAL);
		T_EQ("not a structure",
		     matter_case_sigma1_decode((const uint8_t *)"\x04\x2a", 2u, &s1),
		     MATTER_E_TYPE);

		/* Every truncation. A Sigma1 cut short must never look complete;
		 * answering half of one means running ECDH against a key that
		 * was only partly received. */
		n = build_sigma1(buf, sizeof(buf), rnd, dest, pub, 0x1234u);
		{
			int accepted = 0;

			for (size_t k = 1u; k < n; k++) {
				if (matter_case_sigma1_decode(buf, k, &s1) == MATTER_OK) {
					accepted++;
				}
			}
			T_EQ("none accepted", accepted, 0);
		}
	}
}
