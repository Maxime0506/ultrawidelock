/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_case.h.
 */
#include "matter_case.h"

#include <string.h>

#include "aliro_hash.h"
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
