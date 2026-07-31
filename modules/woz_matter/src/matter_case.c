/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_case.h.
 */
#include "matter_case.h"

#include <string.h>

#include "aliro_hash.h"
#include "matter_crypto.h"
#include "matter_tlv.h"

/* Sigma1 field tags (CASESession.cpp:74-83). */
#define TAG_S1_INITIATOR_RANDOM  1u
#define TAG_S1_INITIATOR_SESSION 2u
#define TAG_S1_DESTINATION_ID    3u
#define TAG_S1_INITIATOR_PUBKEY  4u
#define TAG_S1_RESUMPTION_ID     6u

int matter_case_operational_ipk(const uint8_t epoch_key[MATTER_CASE_IPK_LEN],
				const uint8_t compressed_fabric_id[8],
				uint8_t out[MATTER_CASE_IPK_LEN])
{
	/* "GroupKey v1.0" (crypto/CHIPCryptoPAL.cpp:848). Spelled out so no NUL
	 * can creep into the length -- it is 13 bytes, not 14. */
	static const uint8_t k_info[] = {0x47, 0x72, 0x6F, 0x75, 0x70, 0x4B, 0x65,
					 0x79, 0x20, 0x76, 0x31, 0x2E, 0x30};

	if (epoch_key == NULL || compressed_fabric_id == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}
	if (aliro_hkdf(compressed_fabric_id, 8u, epoch_key, MATTER_CASE_IPK_LEN, k_info,
		       sizeof(k_info), out, MATTER_CASE_IPK_LEN) != 0) {
		return MATTER_E_INVAL;
	}
	return MATTER_OK;
}

int matter_case_destination_id(const uint8_t ipk[MATTER_CASE_IPK_LEN],
			       const uint8_t initiator_random[MATTER_CASE_RANDOM_LEN],
			       const uint8_t root_pub[MATTER_CASE_PUBKEY_LEN], uint64_t fabric_id,
			       uint64_t node_id, uint8_t out[MATTER_CASE_DEST_ID_LEN])
{
	uint8_t msg[MATTER_CASE_RANDOM_LEN + MATTER_CASE_PUBKEY_LEN + 8u + 8u];
	size_t n = 0u;
	size_t i;

	if (ipk == NULL || initiator_random == NULL || root_pub == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}

	memcpy(&msg[n], initiator_random, MATTER_CASE_RANDOM_LEN);
	n += MATTER_CASE_RANDOM_LEN;
	memcpy(&msg[n], root_pub, MATTER_CASE_PUBKEY_LEN);
	n += MATTER_CASE_PUBKEY_LEN;
	/* LITTLE-endian, both of them. */
	for (i = 0u; i < 8u; i++) {
		msg[n + i] = (uint8_t)(fabric_id >> (8u * i));
	}
	n += 8u;
	for (i = 0u; i < 8u; i++) {
		msg[n + i] = (uint8_t)(node_id >> (8u * i));
	}
	n += 8u;

	aliro_hmac_sha256(ipk, MATTER_CASE_IPK_LEN, msg, n, out);
	return MATTER_OK;
}

/** Borrow one octet string of an expected length out of the loaded element. */
static int take_bytes(const struct matter_tlv_reader *r, const uint8_t **out, size_t want)
{
	const uint8_t *p = NULL;
	size_t len = 0u;

	if (matter_tlv_get_bytes(r, &p, &len) != MATTER_OK) {
		return MATTER_E_TYPE;
	}
	if (len != want) {
		return MATTER_E_INVAL;
	}
	*out = p;
	return MATTER_OK;
}

int matter_case_sigma1_decode(const uint8_t *tlv, size_t len, struct matter_case_sigma1 *out)
{
	struct matter_tlv_reader r;
	int rc;

	if (tlv == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}
	memset(out, 0, sizeof(*out));

	matter_tlv_reader_init(&r, tlv, len);
	if (matter_tlv_next(&r) != MATTER_OK ||
	    matter_tlv_element_type(&r) != MATTER_TLV_STRUCTURE) {
		return MATTER_E_TYPE;
	}
	rc = matter_tlv_enter(&r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		uint64_t v = 0u;

		rc = matter_tlv_next(&r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_S1_INITIATOR_RANDOM)) {
			rc = take_bytes(&r, &out->initiator_random, MATTER_CASE_RANDOM_LEN);
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_S1_DESTINATION_ID)) {
			rc = take_bytes(&r, &out->destination_id, MATTER_CASE_DEST_ID_LEN);
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_S1_INITIATOR_PUBKEY)) {
			rc = take_bytes(&r, &out->initiator_pubkey, MATTER_CASE_PUBKEY_LEN);
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_S1_INITIATOR_SESSION)) {
			if (matter_tlv_get_u64(&r, &v) != MATTER_OK || v > UINT16_MAX) {
				return MATTER_E_INVAL;
			}
			out->initiator_session_id = (uint16_t)v;
			rc = MATTER_OK;
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_S1_RESUMPTION_ID)) {
			if (matter_tlv_get_bytes(&r, &out->resumption_id,
						 &out->resumption_id_len) != MATTER_OK) {
				return MATTER_E_TYPE;
			}
			out->has_resumption = true;
			rc = MATTER_OK;
		} else {
			/* Session parameters and the resumption MIC are skipped
			 * rather than refused: a newer initiator may send fields
			 * this node has never heard of, and none of them changes
			 * whether the identity below is the right one. */
			rc = MATTER_OK;
		}

		if (rc != MATTER_OK) {
			return rc;
		}
	}

	/* All three are mandatory, and each has exactly one legal length. A
	 * Sigma1 missing any of them cannot be answered. */
	if (out->initiator_random == NULL || out->destination_id == NULL ||
	    out->initiator_pubkey == NULL) {
		return MATTER_E_INVAL;
	}
	return MATTER_OK;
}

/* --------------------------------------------------------- Sigma2 --- */

/* Sigma2 field tags (CASESession.cpp:85-92). */
#define TAG_S2_RESPONDER_RANDOM  1u
#define TAG_S2_RESPONDER_SESSION 2u
#define TAG_S2_RESPONDER_PUBKEY  3u
#define TAG_S2_ENCRYPTED         4u

/* TBSData and TBEData tags, shared by Sigma2 and Sigma3 (CASESession.cpp:56-72). */
#define TAG_TBS_NOC           1u
#define TAG_TBS_ICAC          2u
#define TAG_TBS_SENDER_PUBKEY 3u
#define TAG_TBS_RECV_PUBKEY   4u

#define TAG_TBE_NOC           1u
#define TAG_TBE_ICAC          2u
#define TAG_TBE_SIGNATURE     3u
#define TAG_TBE_RESUMPTION_ID 4u

/** Length of the S2K salt: IPK, random, ephemeral key, transcript hash. */
#define S2K_SALT_LEN (MATTER_CASE_IPK_LEN + MATTER_CASE_RANDOM_LEN + MATTER_CASE_PUBKEY_LEN + 32u)

/** Resumption identifiers are 16 bytes (CASESession.cpp, kCASEResumptionIDSize). */
#define RESUMPTION_ID_LEN 16u

int matter_case_sigma2_encode(const struct matter_case_sigma2_in *in, uint8_t *out, size_t cap,
			      size_t *out_len, uint8_t shared_out[MATTER_CASE_SECRET_LEN])
{
	/* "Sigma2" and "NCASE_Sigma2N" (CASESession.cpp:128,138). */
	static const uint8_t k_info[] = {0x53, 0x69, 0x67, 0x6D, 0x61, 0x32};
	static const uint8_t k_nonce[MATTER_NONCE_LEN] = {0x4E, 0x43, 0x41, 0x53, 0x45, 0x5F, 0x53,
							  0x69, 0x67, 0x6D, 0x61, 0x32, 0x4E};
	uint8_t salt[S2K_SALT_LEN];
	uint8_t s2k[MATTER_KEY_LEN];
	uint8_t sig[MATTER_CASE_SIG_LEN];
	/* TBSData2 then TBEData2, one after the other in the same scratch: the
	 * first is signed and then no longer needed, and two certificates plus a
	 * signature is most of a kilobyte to hold twice. */
	static uint8_t scratch[MATTER_CASE_SIGMA2_MAX];
	struct matter_tlv_writer w;
	size_t n = 0u;
	size_t off = 0u;
	int rc;

	if (in == NULL || out == NULL || out_len == NULL || shared_out == NULL) {
		return MATTER_E_INVAL;
	}
	if (in->initiator_pubkey == NULL || in->transcript_hash == NULL || in->ipk == NULL ||
	    in->noc == NULL || in->op_priv == NULL || in->responder_random == NULL ||
	    in->responder_eph_priv == NULL || in->responder_eph_pub == NULL ||
	    in->resumption_id == NULL) {
		return MATTER_E_INVAL;
	}

	if (matter_case_ecdh(in->responder_eph_priv, in->initiator_pubkey, shared_out) != 0) {
		return MATTER_E_STATE;
	}

	memcpy(&salt[off], in->ipk, MATTER_CASE_IPK_LEN);
	off += MATTER_CASE_IPK_LEN;
	memcpy(&salt[off], in->responder_random, MATTER_CASE_RANDOM_LEN);
	off += MATTER_CASE_RANDOM_LEN;
	memcpy(&salt[off], in->responder_eph_pub, MATTER_CASE_PUBKEY_LEN);
	off += MATTER_CASE_PUBKEY_LEN;
	memcpy(&salt[off], in->transcript_hash, 32u);
	off += 32u;

	rc = aliro_hkdf(salt, off, shared_out, MATTER_CASE_SECRET_LEN, k_info, sizeof(k_info), s2k,
			sizeof(s2k));
	memset(salt, 0, sizeof(salt));
	if (rc != 0) {
		return MATTER_E_STATE;
	}

	/*
	 * TBSData2. Signed, never transmitted: the peer rebuilds it from what it
	 * already has, which is what makes the signature bind this exchange's
	 * ephemeral keys rather than merely the certificate chain.
	 */
	matter_tlv_writer_init(&w, scratch, sizeof(scratch));
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBS_NOC), in->noc, in->noc_len);
	if (in->icac != NULL && in->icac_len > 0u) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBS_ICAC), in->icac,
					   in->icac_len);
	}
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBS_SENDER_PUBKEY), in->responder_eph_pub,
				   MATTER_CASE_PUBKEY_LEN);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBS_RECV_PUBKEY), in->initiator_pubkey,
				   MATTER_CASE_PUBKEY_LEN);
	(void)matter_tlv_end_container(&w);
	rc = matter_tlv_writer_finish(&w, &n);
	if (rc != MATTER_OK) {
		memset(s2k, 0, sizeof(s2k));
		return rc;
	}

	if (matter_case_sign(in->op_priv, scratch, n, sig) != 0) {
		memset(s2k, 0, sizeof(s2k));
		return MATTER_E_STATE;
	}

	/* TBEData2, over the same scratch now that the signature exists. */
	matter_tlv_writer_init(&w, scratch, sizeof(scratch) - MATTER_TAG_LEN);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBE_NOC), in->noc, in->noc_len);
	if (in->icac != NULL && in->icac_len > 0u) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBE_ICAC), in->icac,
					   in->icac_len);
	}
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBE_SIGNATURE), sig, sizeof(sig));
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_TBE_RESUMPTION_ID), in->resumption_id,
				   RESUMPTION_ID_LEN);
	(void)matter_tlv_end_container(&w);
	rc = matter_tlv_writer_finish(&w, &n);
	if (rc != MATTER_OK) {
		memset(s2k, 0, sizeof(s2k));
		return rc;
	}

	/* Encrypted in place, tag appended. No AAD: Sigma2 has none. */
	rc = matter_aead_encrypt(s2k, k_nonce, NULL, 0u, scratch, n, scratch, scratch + n);
	memset(s2k, 0, sizeof(s2k));
	if (rc != MATTER_OK) {
		return rc;
	}
	n += MATTER_TAG_LEN;

	matter_tlv_writer_init(&w, out, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_S2_RESPONDER_RANDOM),
				   in->responder_random, MATTER_CASE_RANDOM_LEN);
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_S2_RESPONDER_SESSION),
				 in->responder_session_id);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_S2_RESPONDER_PUBKEY),
				   in->responder_eph_pub, MATTER_CASE_PUBKEY_LEN);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_S2_ENCRYPTED), scratch, n);
	(void)matter_tlv_end_container(&w);

	return matter_tlv_writer_finish(&w, out_len);
}
