/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_clusters.h.
 *
 * status() and value() dispatch over the same paths and are kept as two
 * switches rather than one table of function pointers. A table would cost a
 * relocation and an indirect call per attribute on a part where flash is the
 * binding constraint, and the duplication is a case label -- the compiler
 * checks neither way, but a missing case here reads as a missing case.
 */
#include "matter_clusters.h"

#include <stddef.h>
#include <string.h>

/* GeneralCommissioning/Structs.h:41-44 */
#define TAG_BCI_FAILSAFE_EXPIRY 0u
#define TAG_BCI_FAILSAFE_MAX    1u

static bool has_cluster(void *ctx, uint16_t endpoint, uint32_t cluster)
{
	(void)ctx;

	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return false;
	}
	return cluster == MATTER_CLUSTER_BASIC_INFORMATION ||
	       cluster == MATTER_CLUSTER_GENERAL_COMMISSIONING ||
	       cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS;
}

static uint8_t attr_status(void *ctx, uint16_t endpoint, uint32_t cluster, uint32_t attribute)
{
	(void)ctx;

	/*
	 * Endpoint, then cluster, then attribute. The ORDER is the answer:
	 * MetadataLookup.cpp:68-88 reports the outermost thing that is missing,
	 * so a bad endpoint must not be reported as a bad attribute.
	 */
	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT;
	}

	switch (cluster) {
	case MATTER_CLUSTER_BASIC_INFORMATION:
		switch (attribute) {
		case MATTER_ATTR_BASIC_VENDOR_ID:
		case MATTER_ATTR_BASIC_PRODUCT_ID:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_OPERATIONAL_CREDENTIALS:
		switch (attribute) {
		case MATTER_ATTR_OC_SUPPORTED_FABRICS:
		case MATTER_ATTR_OC_COMMISSIONED_FABRICS:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			/* NOCs, Fabrics and TrustedRootCertificates are lists,
			 * and CurrentFabricIndex is scoped to the reading
			 * session's fabric -- which, over PASE, there is not
			 * one of. None has been asked for. */
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_GENERAL_COMMISSIONING:
		switch (attribute) {
		case MATTER_ATTR_GC_BREADCRUMB:
		case MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO:
		case MATTER_ATTR_GC_REGULATORY_CONFIG:
		case MATTER_ATTR_GC_LOCATION_CAPABILITY:
		case MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			/*
			 * IsCommissioningWithoutPower (0x000C) lands here, and a
			 * real iPhone does ask for it. Saying UNSUPPORTED
			 * ATTRIBUTE is the correct answer for a node that does
			 * not implement it, and the commissioner carries on.
			 */
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	default:
		return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
	}
}

static void attr_value(void *ctx, uint16_t endpoint, uint32_t cluster, uint32_t attribute,
		       struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	const struct matter_device_info *info = (const struct matter_device_info *)ctx;

	(void)endpoint; /* attr_status() already refused anything but the root. */

	if (cluster == MATTER_CLUSTER_BASIC_INFORMATION) {
		switch (attribute) {
		case MATTER_ATTR_BASIC_VENDOR_ID:
			(void)matter_tlv_put_u64(w, tag, info->vendor_id);
			return;
		case MATTER_ATTR_BASIC_PRODUCT_ID:
			(void)matter_tlv_put_u64(w, tag, info->product_id);
			return;
		default:
			return;
		}
	}

	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		switch (attribute) {
		case MATTER_ATTR_OC_SUPPORTED_FABRICS:
			(void)matter_tlv_put_u64(w, tag, MATTER_SUPPORTED_FABRICS);
			return;
		case MATTER_ATTR_OC_COMMISSIONED_FABRICS:
			(void)matter_tlv_put_u64(w, tag, info->fabric.index != 0u ? 1u : 0u);
			return;
		default:
			return;
		}
	}

	switch (attribute) {
	case MATTER_ATTR_GC_BREADCRUMB:
		(void)matter_tlv_put_u64(w, tag, info->breadcrumb);
		return;
	case MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO:
		(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_BCI_FAILSAFE_EXPIRY),
					 info->failsafe_expiry_s);
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_BCI_FAILSAFE_MAX),
					 info->failsafe_max_s);
		(void)matter_tlv_end_container(w);
		return;
	case MATTER_ATTR_GC_REGULATORY_CONFIG:
		(void)matter_tlv_put_u64(w, tag, info->regulatory_config);
		return;
	case MATTER_ATTR_GC_LOCATION_CAPABILITY:
		(void)matter_tlv_put_u64(w, tag, info->location_capability);
		return;
	case MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION:
		(void)matter_tlv_put_bool(w, tag, info->supports_concurrent_connection);
		return;
	default:
		return;
	}
}

/*
 * Attribute lists for expanding a wildcard read. These are exactly the
 * attributes attr_status() answers SUCCESS for, and the two must agree: an id
 * here that attr_status() refuses turns a wildcard into a report full of
 * UNSUPPORTED_ATTRIBUTE, which is worse than the silence it replaced.
 *
 * The global attributes (FeatureMap 0xFFFC, ClusterRevision 0xFFFD and the
 * rest) are deliberately absent. Nothing has asked for them, and a wildcard
 * that names them commits this node to answering them individually too.
 */
static const uint32_t k_basic_attrs[] = {
	MATTER_ATTR_BASIC_VENDOR_ID,
	MATTER_ATTR_BASIC_PRODUCT_ID,
};

static const uint32_t k_gc_attrs[] = {
	MATTER_ATTR_GC_BREADCRUMB,
	MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO,
	MATTER_ATTR_GC_REGULATORY_CONFIG,
	MATTER_ATTR_GC_LOCATION_CAPABILITY,
	MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION,
};

static const uint32_t k_oc_attrs[] = {
	MATTER_ATTR_OC_SUPPORTED_FABRICS,
	MATTER_ATTR_OC_COMMISSIONED_FABRICS,
};

static size_t list_attrs(void *ctx, uint16_t endpoint, uint32_t cluster, const uint32_t **out)
{
	(void)ctx;

	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return 0u;
	}
	if (cluster == MATTER_CLUSTER_BASIC_INFORMATION) {
		*out = k_basic_attrs;
		return sizeof(k_basic_attrs) / sizeof(k_basic_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_GENERAL_COMMISSIONING) {
		*out = k_gc_attrs;
		return sizeof(k_gc_attrs) / sizeof(k_gc_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		*out = k_oc_attrs;
		return sizeof(k_oc_attrs) / sizeof(k_oc_attrs[0]);
	}
	return 0u;
}

/** Read one unsigned field out of a command's TLV arguments. */
static bool field_u64(const struct matter_im_invoke *inv, uint8_t tag, uint64_t *out)
{
	struct matter_tlv_reader r;

	if (!inv->has_fields || inv->fields == NULL) {
		return false;
	}
	matter_tlv_reader_init(&r, inv->fields, inv->fields_len);
	if (matter_tlv_next(&r) != MATTER_OK || !matter_tlv_is_container(&r)) {
		return false;
	}
	if (matter_tlv_enter(&r) != MATTER_OK) {
		return false;
	}
	for (;;) {
		int rc = matter_tlv_next(&r);

		if (rc != MATTER_OK) {
			return false;
		}
		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(tag)) {
			return matter_tlv_get_u64(&r, out) == MATTER_OK;
		}
	}
}

/* -------------------------------------- OperationalCredentials --- */

/*
 * Command field tags. Matter numbers a command's arguments from 0 in
 * declaration order, so these are the positions in
 * controller/python/matter/clusters/Objects.py, which spells the tag out
 * rather than leaving it to be counted.
 */
#define TAG_ATTEST_NONCE 0u
#define TAG_CERT_TYPE    0u
#define TAG_CSR_NONCE    0u

#define TAG_ADDNOC_NOC                0u
#define TAG_ADDNOC_ICAC               1u
#define TAG_ADDNOC_IPK                2u
#define TAG_ADDNOC_CASE_ADMIN_SUBJECT 3u
#define TAG_ADDNOC_ADMIN_VENDOR_ID    4u

#define TAG_ADDROOT_CERT 0u

/* Response field tags, same source. */
#define TAG_RESP_ELEMENTS  0u
#define TAG_RESP_SIGNATURE 1u
#define TAG_RESP_CERT      0u

#define TAG_NOCRESP_STATUS       0u
#define TAG_NOCRESP_FABRIC_INDEX 1u

/** Borrow one octet-string field out of a command's arguments. */
static bool field_bytes(const struct matter_im_invoke *inv, uint8_t tag, const uint8_t **out,
			size_t *len)
{
	struct matter_tlv_reader r;

	if (!inv->has_fields || inv->fields == NULL) {
		return false;
	}
	matter_tlv_reader_init(&r, inv->fields, inv->fields_len);
	if (matter_tlv_next(&r) != MATTER_OK || !matter_tlv_is_container(&r)) {
		return false;
	}
	if (matter_tlv_enter(&r) != MATTER_OK) {
		return false;
	}
	for (;;) {
		if (matter_tlv_next(&r) != MATTER_OK) {
			return false;
		}
		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(tag)) {
			return matter_tlv_get_bytes(&r, out, len) == MATTER_OK;
		}
	}
}

/**
 * Install the root the commissioner wants this node to trust.
 *
 * Only the public key is kept -- see matter_fabric.h. Nothing is verified: this
 * node has no prior opinion about which roots are legitimate, which is exactly
 * what makes it commissionable.
 */
static uint8_t add_trusted_root(struct matter_device_info *info, const struct matter_im_invoke *inv)
{
	const uint8_t *cert = NULL;
	size_t cert_len = 0u;
	struct matter_cert_info ci;

	if (!field_bytes(inv, TAG_ADDROOT_CERT, &cert, &cert_len) || cert_len > MATTER_CERT_MAX) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}
	if (matter_cert_parse(cert, cert_len, &ci) != MATTER_OK || !ci.have_public_key) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}

	memcpy(info->fabric.root_public_key, ci.public_key, sizeof(ci.public_key));
	info->fabric.have_root = true;
	return MATTER_IM_STATUS_SUCCESS;
}

/**
 * Accept the operational identity the commissioner minted for this node.
 *
 * @return the NodeOperationalCertStatusEnum for the reply. Every refusal is one
 *         of these rather than an IM status, because each names WHICH input was
 *         wrong and a commissioner can act on that.
 */
static uint8_t add_noc(struct matter_device_info *info, const struct matter_im_invoke *inv)
{
	const uint8_t *noc = NULL;
	const uint8_t *icac = NULL;
	const uint8_t *ipk = NULL;
	size_t noc_len = 0u;
	size_t icac_len = 0u;
	size_t ipk_len = 0u;
	struct matter_cert_info ci;
	uint64_t v = 0u;

	if (!info->have_op_key) {
		/* No CSR, so there is no private key behind whatever public key
		 * this NOC certifies. */
		return MATTER_NOC_STATUS_MISSING_CSR;
	}
	if (!info->fabric.have_root) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}
	if (info->fabric.index != 0u) {
		return MATTER_NOC_STATUS_TABLE_FULL;
	}

	if (!field_bytes(inv, TAG_ADDNOC_NOC, &noc, &noc_len) || noc_len > MATTER_CERT_MAX ||
	    !field_bytes(inv, TAG_ADDNOC_IPK, &ipk, &ipk_len) || ipk_len != MATTER_IPK_LEN) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}
	if (matter_cert_parse(noc, noc_len, &ci) != MATTER_OK || !ci.have_node_id ||
	    !ci.have_fabric_id || !ci.have_public_key) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}
	/*
	 * The certified key must be the one this node minted for the CSR.
	 * Installing an identity whose private half this node does not hold
	 * would look like success here and surface much later as a CASE that
	 * never completes, with nothing to point at.
	 */
	if (memcmp(ci.public_key, info->op_pub, sizeof(info->op_pub)) != 0) {
		return MATTER_NOC_STATUS_INVALID_PUBLIC_KEY;
	}
	/* Optional: absent when the commissioner signed the NOC with its root
	 * directly, which is what Apple does. */
	(void)field_bytes(inv, TAG_ADDNOC_ICAC, &icac, &icac_len);
	if (icac_len > MATTER_CERT_MAX) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}

	memcpy(info->fabric.noc, noc, noc_len);
	info->fabric.noc_len = noc_len;
	if (icac_len != 0u) {
		memcpy(info->fabric.icac, icac, icac_len);
	}
	info->fabric.icac_len = icac_len;
	memcpy(info->fabric.ipk, ipk, ipk_len);
	info->fabric.node_id = ci.node_id;
	info->fabric.fabric_id = ci.fabric_id;
	if (field_u64(inv, TAG_ADDNOC_CASE_ADMIN_SUBJECT, &v)) {
		info->fabric.case_admin_subject = v;
	}
	if (field_u64(inv, TAG_ADDNOC_ADMIN_VENDOR_ID, &v) && v <= UINT16_MAX) {
		info->fabric.admin_vendor_id = (uint16_t)v;
	}
	info->fabric.index = 1u;
	return MATTER_NOC_STATUS_OK;
}

/**
 * Run one OperationalCredentials command.
 *
 * Everything expensive happens here -- the signature, and for a CSR a fresh
 * P-256 key pair -- because this runs exactly once per request while
 * opcred_fields() may not.
 */
static uint8_t opcred_command(struct matter_device_info *info, const struct matter_im_invoke *inv,
			      uint32_t *response_command)
{
	const uint8_t *nonce = NULL;
	size_t nonce_len = 0u;
	uint64_t v = 0u;
	int rc;

	switch (inv->command) {
	case MATTER_CMD_OC_CERTIFICATE_CHAIN_REQUEST: {
		const uint8_t *cert = NULL;
		size_t cert_len = 0u;

		if (!field_u64(inv, TAG_CERT_TYPE, &v) || v > UINT8_MAX) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/* Checked here rather than when writing the reply, because a
		 * reply has no way to say "no such certificate". */
		if (matter_attest_cert((uint8_t)v, &cert, &cert_len) != MATTER_OK) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		info->cert_type = (uint8_t)v;
		*response_command = MATTER_CMD_OC_CERTIFICATE_CHAIN_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;
	}

	case MATTER_CMD_OC_ATTESTATION_REQUEST:
		if (!info->have_challenge) {
			/* Nothing to bind the signature to. Refusing beats
			 * signing something a recorded session could reuse. */
			return MATTER_IM_STATUS_FAILURE;
		}
		if (!field_bytes(inv, TAG_ATTEST_NONCE, &nonce, &nonce_len) ||
		    nonce_len != MATTER_ATTEST_NONCE_LEN) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/* No clock on this node, and 0 is what a device without one
		 * sends -- not a placeholder for something better. */
		rc = matter_attest_elements_encode(nonce, nonce_len, 0u, info->attest_buf,
						   MATTER_ATTEST_ELEMENTS_MAX, &info->attest_len);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_sign_with_challenge(
			info->attest_buf, info->attest_len, sizeof(info->attest_buf),
			info->attestation_challenge, sizeof(info->attestation_challenge),
			info->attest_sig);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		*response_command = MATTER_CMD_OC_ATTESTATION_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_OC_CSR_REQUEST: {
		uint8_t csr[MATTER_CSR_MAX];
		size_t csr_len = 0u;

		if (!info->have_challenge) {
			return MATTER_IM_STATUS_FAILURE;
		}
		if (!field_bytes(inv, TAG_CSR_NONCE, &nonce, &nonce_len) ||
		    nonce_len != MATTER_ATTEST_NONCE_LEN) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/*
		 * A FRESH key every time. Reusing one across commissioning
		 * attempts would let a fabric that saw an earlier CSR recognise
		 * the node on another.
		 */
		if (matter_attest_ec_keygen(info->op_priv, info->op_pub) != 0) {
			return MATTER_IM_STATUS_FAILURE;
		}
		info->have_op_key = true;
		if (matter_attest_csr(info->op_priv, info->op_pub, csr, sizeof(csr), &csr_len) !=
		    MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_nocsr_encode(csr, csr_len, nonce, nonce_len, info->attest_buf,
						MATTER_ATTEST_ELEMENTS_MAX, &info->attest_len);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_sign_with_challenge(
			info->attest_buf, info->attest_len, sizeof(info->attest_buf),
			info->attestation_challenge, sizeof(info->attestation_challenge),
			info->attest_sig);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		*response_command = MATTER_CMD_OC_CSR_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;
	}

	case MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE:
		/*
		 * Both of the commands below change what this node believes
		 * about who owns it, which is precisely what a fail-safe exists
		 * to be able to undo. Doing either outside one would leave a
		 * half-installed identity with nothing scheduled to remove it.
		 */
		if (!info->failsafe_armed) {
			return MATTER_IM_STATUS_FAILSAFE_REQUIRED;
		}
		/* No response command: the reply is a bare SUCCESS status. */
		*response_command = MATTER_IM_NO_RESPONSE;
		return add_trusted_root(info, inv);

	case MATTER_CMD_OC_ADD_NOC:
		if (!info->failsafe_armed) {
			return MATTER_IM_STATUS_FAILSAFE_REQUIRED;
		}
		info->last_noc_status = add_noc(info, inv);
		*response_command = MATTER_CMD_OC_NOC_RESPONSE;
		/* SUCCESS means "a NOCResponse follows", not "the NOC was
		 * accepted"; last_noc_status carries the verdict. */
		return MATTER_IM_STATUS_SUCCESS;

	default:
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
}

/** Serialise what opcred_command() already computed. */
static void opcred_fields(const struct matter_device_info *info, uint32_t response_command,
			  struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);

	if (response_command == MATTER_CMD_OC_CERTIFICATE_CHAIN_RESPONSE) {
		const uint8_t *cert = NULL;
		size_t cert_len = 0u;

		if (matter_attest_cert(info->cert_type, &cert, &cert_len) == MATTER_OK) {
			(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_CERT), cert,
						   cert_len);
		}
	} else if (response_command == MATTER_CMD_OC_NOC_RESPONSE) {
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NOCRESP_STATUS),
					 info->last_noc_status);
		/*
		 * FabricIndex only on success, and DebugText not at all. Both
		 * are optional and CHIP's own device omits them the same way
		 * (operational-credentials-cluster.cpp, SendNOCResponse) -- an
		 * index for a fabric that was not created would be a number the
		 * commissioner could act on.
		 */
		if (info->last_noc_status == MATTER_NOC_STATUS_OK) {
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NOCRESP_FABRIC_INDEX),
						 info->fabric.index);
		}
	} else {
		/* AttestationResponse and CSRResponse are the same shape: the
		 * elements, then the signature over them. */
		(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_ELEMENTS), info->attest_buf,
					   info->attest_len);
		(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_SIGNATURE), info->attest_sig,
					   sizeof(info->attest_sig));
	}

	(void)matter_tlv_end_container(w);
}

static uint8_t command(void *ctx, const struct matter_im_invoke *inv, uint32_t *response_command)
{
	struct matter_device_info *info = (struct matter_device_info *)ctx;
	uint64_t v = 0u;

	if (inv->endpoint != MATTER_ENDPOINT_ROOT) {
		return MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT;
	}
	if (inv->cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		return opcred_command(info, inv, response_command);
	}
	if (inv->cluster != MATTER_CLUSTER_GENERAL_COMMISSIONING) {
		return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
	}

	switch (inv->command) {
	case MATTER_CMD_GC_ARM_FAIL_SAFE:
		/*
		 * The breadcrumb is the commissioner's own progress marker: it
		 * sets it here and reads it back if it has to resume, so losing
		 * it makes a retry restart from nothing.
		 */
		if (field_u64(inv, 1u, &v)) {
			info->breadcrumb = v;
		}
		info->failsafe_armed = true;
		info->last_commissioning_error = MATTER_COMMISSIONING_OK;
		*response_command = MATTER_CMD_GC_ARM_FAIL_SAFE_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_GC_SET_REGULATORY_CONFIG:
		if (field_u64(inv, 0u, &v)) {
			/* Accept only what LocationCapability claims to support.
			 * Saying yes to a location this node cannot honour is a
			 * lie the commissioner has no way to detect. */
			if (v != info->location_capability &&
			    info->location_capability != MATTER_REGULATORY_INDOOR_OUTDOOR) {
				info->last_commissioning_error =
					MATTER_COMMISSIONING_VALUE_OUTSIDE_RANGE;
				*response_command = MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE;
				return MATTER_IM_STATUS_SUCCESS;
			}
			info->regulatory_config = (uint8_t)v;
		}
		if (field_u64(inv, 2u, &v)) {
			info->breadcrumb = v;
		}
		info->last_commissioning_error = MATTER_COMMISSIONING_OK;
		*response_command = MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_GC_COMMISSIONING_COMPLETE:
		/*
		 * Answered NO_FAIL_SAFE rather than OK, because it would be a
		 * lie: completing commissioning means having accepted an
		 * operational identity over CASE, and nothing here has. The
		 * commissioner is told the truth and fails cleanly instead of
		 * believing it owns a node that cannot be reached.
		 */
		info->last_commissioning_error = MATTER_COMMISSIONING_NO_FAIL_SAFE;
		*response_command = MATTER_CMD_GC_COMMISSIONING_COMPLETE_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	default:
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
}

static void command_fields(void *ctx, uint16_t endpoint, uint32_t cluster,
			   uint32_t response_command, struct matter_tlv_writer *w,
			   matter_tlv_tag_t tag)
{
	const struct matter_device_info *info = (const struct matter_device_info *)ctx;

	(void)endpoint;

	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		opcred_fields(info, response_command, w, tag);
		return;
	}
	(void)response_command;

	/*
	 * All three GeneralCommissioning responses carry the same two fields:
	 * ErrorCode then DebugText (Commands.h:133-134, 209-210). DebugText is
	 * mandatory and empty, not omitted -- a missing mandatory field is a
	 * decode failure at the commissioner, which presents as a hang.
	 */
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(0u), info->last_commissioning_error);
	(void)matter_tlv_put_utf8(w, MATTER_TLV_CTX(1u), "", 0u);
	(void)matter_tlv_end_container(w);
}

void matter_clusters_init(struct matter_im_server *srv, struct matter_device_info *info)
{
	if (srv == NULL) {
		return;
	}
	srv->status = attr_status;
	srv->value = attr_value;
	srv->has_cluster = has_cluster;
	srv->list_attrs = list_attrs;
	srv->command = command;
	srv->command_fields = command_fields;
	srv->ctx = info;
}
