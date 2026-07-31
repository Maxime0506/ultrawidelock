/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_fabric.h.
 */
#include "matter_fabric.h"

#include <string.h>

#include "matter_tlv.h"

/* Certificate element tags (credentials/CHIPCert.h:68-78). */
#define CERT_TAG_SUBJECT    6u
#define CERT_TAG_PUBLIC_KEY 9u

/*
 * Distinguished-name attribute tags.
 *
 * The context tag number IS the attribute's OID enum -- 17 for matter-node-id,
 * 21 for matter-fabric-id (lib/asn1/gen_asn1oid.py:137,145) -- with bit 0x80
 * set when the value is a printable string instead of an integer
 * (credentials/CHIPCert.cpp:755-758). Both of these are integers, so the flag
 * is never set on them and a tag carrying it is a different attribute, not
 * these ones spelled differently.
 */
#define DN_TAG_MATTER_NODE_ID   17u
#define DN_TAG_MATTER_FABRIC_ID 21u

/** Pull the node and fabric ids out of a subject DN the reader is sitting on. */
static int parse_subject(struct matter_tlv_reader *r, struct matter_cert_info *out)
{
	int rc = matter_tlv_enter(r);

	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		uint64_t v = 0u;

		rc = matter_tlv_next(r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(r) == MATTER_TLV_CTX(DN_TAG_MATTER_NODE_ID)) {
			if (matter_tlv_get_u64(r, &v) != MATTER_OK) {
				return MATTER_E_TYPE;
			}
			out->node_id = v;
			out->have_node_id = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(DN_TAG_MATTER_FABRIC_ID)) {
			if (matter_tlv_get_u64(r, &v) != MATTER_OK) {
				return MATTER_E_TYPE;
			}
			out->fabric_id = v;
			out->have_fabric_id = true;
		}
		/* Every other attribute -- the common name a commissioner may
		 * add, the CASE authenticated tags -- is skipped by next(). */
	}

	return matter_tlv_exit(r);
}

int matter_cert_parse(const uint8_t *cert, size_t len, struct matter_cert_info *out)
{
	struct matter_tlv_reader r;
	int rc;

	if (cert == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}
	memset(out, 0, sizeof(*out));

	matter_tlv_reader_init(&r, cert, len);
	if (matter_tlv_next(&r) != MATTER_OK || !matter_tlv_is_container(&r)) {
		return MATTER_E_TYPE;
	}
	rc = matter_tlv_enter(&r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		rc = matter_tlv_next(&r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(CERT_TAG_PUBLIC_KEY)) {
			const uint8_t *key = NULL;
			size_t key_len = 0u;

			if (matter_tlv_get_bytes(&r, &key, &key_len) != MATTER_OK) {
				return MATTER_E_TYPE;
			}
			/* A P-256 certificate whose key is not a P-256 point is
			 * malformed, not merely uninteresting. */
			if (key_len != sizeof(out->public_key)) {
				return MATTER_E_INVAL;
			}
			memcpy(out->public_key, key, key_len);
			out->have_public_key = true;
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(CERT_TAG_SUBJECT) &&
			   matter_tlv_is_container(&r)) {
			rc = parse_subject(&r, out);
			if (rc != MATTER_OK) {
				return rc;
			}
		}
	}

	return MATTER_OK;
}
