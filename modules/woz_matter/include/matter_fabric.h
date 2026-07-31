/**
 * @file matter_fabric.h — the operational identity a commissioner installs.
 *
 * Attestation ends with the commissioner holding a public key this node proved
 * it owns. What follows is the commissioner handing back an identity built on
 * that key:
 *
 *   AddTrustedRootCertificate  trust this root
 *   AddNOC                     and here is who you are underneath it
 *
 * Both certificates arrive as MATTER TLV, not X.509. The spec defines a
 * compressed form precisely so a constrained node can read one without an
 * ASN.1 decoder, and this file is that reader.
 *
 * It reads three things and ignores the rest: the subject's node id, its fabric
 * id, and the public key. Validity dates, key usage and the signature are what
 * a node checks when VERIFYING a certificate somebody else presents, which is
 * CASE's job. A commissioner has no reason to lie to itself about a NOC it just
 * minted, and this node cannot check the signature anyway without the issuer's
 * key -- which, for the NOC, is the root it was told to trust one command
 * earlier and has no independent reason to believe.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 6 of internal/cdk-matter-plan.md.
 *
 * Certificate element tags from workspace/modules/lib/matter/src/credentials/
 * CHIPCert.h:68-78; distinguished-name attribute tags from
 * src/lib/asn1/gen_asn1oid.py:137-148, encoded as context tags by
 * credentials/CHIPCert.cpp:755-763.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_status.h"

/** kMaxCHIPCertLength (credentials/CHIPCert.h:54). */
#define MATTER_CERT_MAX 400u

/** The identity protection key: one AES-128 key, shared by a whole fabric. */
#define MATTER_IPK_LEN 16u

/** Uncompressed P-256 point, 0x04 || X || Y. */
#define MATTER_FABRIC_PUBKEY_LEN 65u

/** Compressed fabric identifier: 64 bits (crypto/CHIPCryptoPAL.cpp:796-833). */
#define MATTER_COMPRESSED_FABRIC_LEN 8u

/**
 * The operational instance name, "%08X%08X-%08X%08X" plus its NUL --
 * compressed fabric id, hyphen, node id, 32 uppercase hex digits in total
 * (lib/dnssd/ServiceNaming.cpp, MakeInstanceName).
 */
#define MATTER_INSTANCE_NAME_LEN 34u

/** What matter_cert_parse() found. Absent fields leave their have_* flag false. */
struct matter_cert_info {
	uint64_t node_id;
	uint64_t fabric_id;
	uint8_t public_key[MATTER_FABRIC_PUBKEY_LEN];
	bool have_node_id;
	bool have_fabric_id;
	bool have_public_key;
};

/**
 * Read the interesting fields out of a Matter operational certificate.
 *
 * @return MATTER_OK if the TLV parsed, whatever the certificate turned out to
 *         contain -- the caller decides which fields it needed. An error means
 *         the bytes were not a well-formed certificate at all.
 */
int matter_cert_parse(const uint8_t *cert, size_t len, struct matter_cert_info *out);

/**
 * One fabric's worth of operational identity.
 *
 * Held in RAM and nothing more. A fabric is supposed to survive a reboot, and
 * this one does not; there is no settings backend on this port yet, and the
 * fail-safe would roll an incomplete commissioning back regardless. What it
 * does have to survive is the gap between AddNOC and CASE, which is the same
 * boot.
 *
 * The trusted root is kept as a public key rather than as the certificate it
 * arrived in: verifying a peer's NOC chain needs the key, and nothing this node
 * does needs the other 300-odd bytes. The ICAC is kept whole because CASE has
 * to send it back out.
 */
struct matter_fabric {
	/** 0 when empty. Fabric indices start at 1. */
	uint8_t index;
	/** True once AddTrustedRootCertificate has been accepted. */
	bool have_root;
	uint64_t fabric_id;
	uint64_t node_id;
	/** The subject the commissioner wants granted administer privilege. */
	uint64_t case_admin_subject;
	uint16_t admin_vendor_id;
	uint8_t root_public_key[MATTER_FABRIC_PUBKEY_LEN];
	uint8_t ipk[MATTER_IPK_LEN];
	uint8_t noc[MATTER_CERT_MAX];
	size_t noc_len;
	/** Empty when the commissioner signed the NOC with the root directly. */
	uint8_t icac[MATTER_CERT_MAX];
	size_t icac_len;
};

/**
 * Derive the compressed fabric identifier (crypto/CHIPCryptoPAL.cpp:796-833).
 *
 *   HKDF-SHA256(ikm  = root public key WITHOUT its 0x04 prefix,
 *               salt = fabric id, 8 bytes big-endian,
 *               info = "CompressedFabric",
 *               len  = 8)
 *
 * Two fabrics can share a fabric id -- it is chosen by whoever built them --
 * so this mixes in the root public key to get something that identifies a
 * fabric on the wire without being guessable from its number alone.
 *
 * The 0x04 is dropped because it says only that the point is uncompressed; it
 * is the same byte for every key and carries nothing to derive from.
 *
 * @param root_pub uncompressed, and refused if it does not start with 0x04.
 * @return MATTER_OK, or MATTER_E_INVAL.
 */
int matter_fabric_compressed_id(const uint8_t root_pub[MATTER_FABRIC_PUBKEY_LEN],
				uint64_t fabric_id, uint8_t out[MATTER_COMPRESSED_FABRIC_LEN]);

/**
 * Write the DNS-SD instance name a commissioner looks this node up by.
 *
 * "<compressed-fabric-id>-<node-id>", 16 uppercase hex digits each, which is
 * the instance part of <name>._matter._tcp.local.
 *
 * @param out at least MATTER_INSTANCE_NAME_LEN bytes; NUL-terminated.
 * @return MATTER_OK, or MATTER_E_INVAL.
 */
int matter_fabric_instance_name(const struct matter_fabric *fabric, char *out, size_t cap);
