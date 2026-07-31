/**
 * @file matter_case.h — proving an operational identity, both ways.
 *
 * PASE let a commissioner in because it knew a printed code. CASE is what
 * happens afterwards, every time: two nodes that already hold certificates from
 * the same fabric prove it to each other and agree on session keys. It is the
 * only session type the spec will accept CommissioningComplete over, and the
 * only way a phone talks to this node once BLE is gone.
 *
 *   Sigma1  initiator -> responder   who I want, and my ephemeral key
 *   Sigma2  responder -> initiator   my certificate chain, signed, encrypted
 *   Sigma3  initiator -> responder   the same, in the other direction
 *
 * This file is the responder's half, built in that order.
 *
 * The subtle piece is Sigma1's destinationId. It is not an address: it is an
 * HMAC that only somebody holding the fabric's identity protection key could
 * have produced, over the identity they are asking for. A responder does not
 * read a node id out of it -- it recomputes the HMAC for each fabric it holds
 * and looks for a match. That is what makes an unsolicited Sigma1 unable to
 * enumerate a node's fabrics: get the key wrong and you learn nothing.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 8 of internal/cdk-matter-plan.md.
 *
 * Derivations transcribed from workspace/modules/lib/matter/src:
 * protocols/secure_channel/CASEDestinationId.cpp for the destination
 * identifier, crypto/CHIPCryptoPAL.cpp:848-860 for the operational key, and
 * protocols/secure_channel/CASESession.cpp:74-83 for the Sigma1 tags.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Secure Channel opcodes for CASE (protocols/secure_channel/Constants.h:68-71).
 */
#define MATTER_OP_CASE_SIGMA1 0x30u
#define MATTER_OP_CASE_SIGMA2 0x31u
#define MATTER_OP_CASE_SIGMA3 0x32u

/** Both random values, and the destination identifier, are SHA-256 sized. */
#define MATTER_CASE_RANDOM_LEN  32u
#define MATTER_CASE_DEST_ID_LEN 32u

/** An operational group key, and the epoch key it comes from. */
#define MATTER_CASE_IPK_LEN 16u

/** Uncompressed P-256 point. */
#define MATTER_CASE_PUBKEY_LEN 65u

/**
 * Derive the operational identity protection key from the epoch key.
 *
 *   HKDF-SHA256(ikm  = the IPK AddNOC delivered,
 *               salt = the compressed fabric id,
 *               info = "GroupKey v1.0",
 *               len  = 16)
 *
 * AddNOC hands over an EPOCH key, and every use of "the IPK" in CASE means the
 * operational key derived from it -- CHIP's own GetIpkKeySet() returns
 * operational_keys[].encryption_key, not the epoch key it was given
 * (credentials/GroupDataProviderImpl.cpp). Skipping this step produces a
 * destination identifier that never matches, with nothing to say why.
 */
int matter_case_operational_ipk(const uint8_t epoch_key[MATTER_CASE_IPK_LEN],
				const uint8_t compressed_fabric_id[8],
				uint8_t out[MATTER_CASE_IPK_LEN]);

/**
 * Recompute the destination identifier an initiator claims.
 *
 *   HMAC-SHA256(key = operational IPK,
 *               msg = initiatorRandom || rootPublicKey || fabricId || nodeId)
 *
 * fabricId and nodeId are LITTLE-endian here, which is worth stating because
 * the compressed fabric identifier salts with the fabric id BIG-endian. Two
 * derivations, one field, opposite orders; both encode cleanly and only one
 * matches a real phone.
 *
 * @param root_pub uncompressed, 65 bytes, INCLUDING its 0x04 -- unlike the
 *        compressed fabric id, which drops it.
 */
int matter_case_destination_id(const uint8_t ipk[MATTER_CASE_IPK_LEN],
			       const uint8_t initiator_random[MATTER_CASE_RANDOM_LEN],
			       const uint8_t root_pub[MATTER_CASE_PUBKEY_LEN], uint64_t fabric_id,
			       uint64_t node_id, uint8_t out[MATTER_CASE_DEST_ID_LEN]);

/** What Sigma1 carries. Pointers borrow the caller's buffer; nothing is copied. */
struct matter_case_sigma1 {
	const uint8_t *initiator_random; /**< 32 bytes. */
	const uint8_t *destination_id;   /**< 32 bytes. */
	const uint8_t *initiator_pubkey; /**< 65 bytes. */
	uint16_t initiator_session_id;
	/** Present when the initiator is offering to resume an earlier session. */
	const uint8_t *resumption_id;
	size_t resumption_id_len;
	bool has_resumption;
};

/**
 * Decode a Sigma1 (CASESession.cpp:74-83).
 *
 * The three fixed-length fields are checked against their lengths rather than
 * merely read: a Sigma1 whose ephemeral key is not a P-256 point cannot lead
 * anywhere, and refusing it here is cheaper than discovering it inside ECDH.
 *
 * @return MATTER_OK, MATTER_E_INVAL if a mandatory field is missing or
 *         mis-sized, or whatever the TLV decoder returned.
 */
int matter_case_sigma1_decode(const uint8_t *tlv, size_t len, struct matter_case_sigma1 *out);

#ifdef __cplusplus
}
#endif
