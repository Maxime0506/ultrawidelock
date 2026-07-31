/**
 * @file test_matter_fabric.c — reading real certificates, and installing one.
 *
 * The certificates here are not hand-built. They are CHIP's own test vectors,
 * sTestCert_Root01_Chip and sTestCert_Node01_01_Chip out of
 * credentials/tests/CHIPCert_test_vectors.cpp, produced by chip-cert and used
 * by the SDK's own certificate suite. The node and fabric ids asserted below
 * are the constants that file declares alongside them
 * (CHIPCert_test_vectors.h:143-144), so a parser that agrees with itself but
 * not with the format still fails here.
 *
 * The two certificates differ in a way that matters: the node certificate's
 * subject carries matter-node-id (17) and matter-fabric-id (21), while the
 * root's carries matter-rcac-id (20) and neither of the other two. A parser
 * that read distinguished-name attributes without checking which one it was
 * looking at would report a node id for the root, and this catches that.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>

#include "matter_clusters.h"
#include "matter_fabric.h"
#include "matter_im.h"
#include "matter_tlv.h"

#include "test.h"

static const uint8_t k_root01[] = {
	0x15, 0x30, 0x01, 0x08, 0x53, 0x4C, 0x45, 0x82, 0x73, 0x62, 0x35, 0x14, 0x24, 0x02, 0x01,
	0x37, 0x03, 0x27, 0x14, 0x01, 0x00, 0x00, 0x00, 0xCA, 0xCA, 0xCA, 0xCA, 0x18, 0x26, 0x04,
	0xEF, 0x17, 0x1B, 0x27, 0x26, 0x05, 0x6E, 0xB5, 0xB9, 0x4C, 0x37, 0x06, 0x27, 0x14, 0x01,
	0x00, 0x00, 0x00, 0xCA, 0xCA, 0xCA, 0xCA, 0x18, 0x24, 0x07, 0x01, 0x24, 0x08, 0x01, 0x30,
	0x09, 0x41, 0x04, 0x3B, 0x88, 0x46, 0x0E, 0xC9, 0x68, 0x7A, 0x5D, 0x0F, 0x3B, 0x4B, 0x3B,
	0x13, 0xFC, 0xD2, 0x99, 0xC2, 0xF6, 0xD5, 0x05, 0x1D, 0x00, 0x3E, 0xE4, 0x9C, 0x99, 0x24,
	0xCF, 0x98, 0xF4, 0xF7, 0x80, 0xEB, 0x20, 0xFD, 0x37, 0xC8, 0xD3, 0x58, 0x34, 0x7F, 0x5F,
	0x87, 0xD0, 0x8C, 0x32, 0x13, 0xE5, 0x40, 0xAF, 0x11, 0xBA, 0xB9, 0x13, 0x7E, 0x49, 0x35,
	0x4F, 0x0C, 0x5B, 0x63, 0x43, 0xDE, 0x63, 0x37, 0x0A, 0x35, 0x01, 0x29, 0x01, 0x18, 0x24,
	0x02, 0x60, 0x30, 0x04, 0x14, 0xCC, 0x13, 0x08, 0xAF, 0x82, 0xCF, 0xEE, 0x50, 0x5E, 0xB2,
	0x3B, 0x57, 0xBF, 0xE8, 0x6A, 0x31, 0x16, 0x65, 0x53, 0x5F, 0x30, 0x05, 0x14, 0xCC, 0x13,
	0x08, 0xAF, 0x82, 0xCF, 0xEE, 0x50, 0x5E, 0xB2, 0x3B, 0x57, 0xBF, 0xE8, 0x6A, 0x31, 0x16,
	0x65, 0x53, 0x5F, 0x18, 0x30, 0x0B, 0x40, 0xF7, 0xF0, 0x09, 0x26, 0x90, 0x49, 0x4E, 0x46,
	0xC8, 0xB1, 0xC5, 0xCB, 0xD1, 0xA5, 0x08, 0x5E, 0x1E, 0x65, 0xD4, 0x36, 0x0F, 0x98, 0xE9,
	0x6C, 0x4E, 0x8E, 0x49, 0x5D, 0xC5, 0xE2, 0x16, 0xD0, 0xBF, 0xA2, 0x3D, 0x8F, 0x57, 0x47,
	0x0D, 0x89, 0xFD, 0xDA, 0xF0, 0x3F, 0x04, 0x64, 0xB0, 0xAE, 0x8E, 0x1F, 0x95, 0x6D, 0x6F,
	0x67, 0xA3, 0x11, 0x24, 0x38, 0x58, 0x24, 0x68, 0x97, 0x80, 0xA9, 0x18,
};

static const uint8_t k_node01[] = {
	0x15, 0x30, 0x01, 0x08, 0x18, 0xE9, 0x69, 0xBA, 0x0E, 0x08, 0x9E, 0x23, 0x24, 0x02, 0x01,
	0x37, 0x03, 0x27, 0x13, 0x03, 0x00, 0x00, 0x00, 0xCA, 0xCA, 0xCA, 0xCA, 0x18, 0x26, 0x04,
	0xEF, 0x17, 0x1B, 0x27, 0x26, 0x05, 0x6E, 0xB5, 0xB9, 0x4C, 0x37, 0x06, 0x27, 0x11, 0x01,
	0x00, 0x01, 0x00, 0xDE, 0xDE, 0xDE, 0xDE, 0x27, 0x15, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xB0, 0xFA, 0x18, 0x24, 0x07, 0x01, 0x24, 0x08, 0x01, 0x30, 0x09, 0x41, 0x04, 0xBC, 0xF6,
	0x58, 0x0D, 0x2D, 0x71, 0xE1, 0x44, 0x16, 0x65, 0x1F, 0x7C, 0x31, 0x1B, 0x5E, 0xFC, 0xF9,
	0xAE, 0xC0, 0xA8, 0xC1, 0x0A, 0xF8, 0x09, 0x27, 0x84, 0x4C, 0x24, 0x0F, 0x51, 0xA8, 0xEB,
	0x23, 0xFA, 0x07, 0x44, 0x13, 0x88, 0x87, 0xAC, 0x1E, 0x73, 0xCB, 0x72, 0xA0, 0x54, 0xB6,
	0xA0, 0xDB, 0x06, 0x22, 0xAA, 0x80, 0x70, 0x71, 0x01, 0x63, 0x13, 0xB1, 0x59, 0x6C, 0x85,
	0x52, 0xCF, 0x37, 0x0A, 0x35, 0x01, 0x28, 0x01, 0x18, 0x24, 0x02, 0x01, 0x36, 0x03, 0x04,
	0x02, 0x04, 0x01, 0x18, 0x30, 0x04, 0x14, 0x69, 0x67, 0xC9, 0x12, 0xF8, 0xA3, 0xE6, 0x89,
	0x55, 0x6F, 0x89, 0x9B, 0x65, 0xD7, 0x6F, 0x53, 0xFA, 0x65, 0xC7, 0xB6, 0x30, 0x05, 0x14,
	0x44, 0x0C, 0xC6, 0x92, 0x31, 0xC4, 0xCB, 0x5B, 0x37, 0x94, 0x24, 0x26, 0xF8, 0x1B, 0xBE,
	0x24, 0xB7, 0xEF, 0x34, 0x5C, 0x18, 0x30, 0x0B, 0x40, 0xCE, 0x6E, 0xF3, 0x93, 0xCB, 0xBC,
	0x94, 0xF8, 0x0E, 0xE2, 0x90, 0xCB, 0x3C, 0x3D, 0x37, 0x33, 0x35, 0xBA, 0xB9, 0x59, 0x07,
	0x73, 0x4D, 0x99, 0xD3, 0x84, 0xA6, 0x2A, 0x37, 0x3B, 0x84, 0x84, 0xE1, 0xD4, 0x1A, 0x04,
	0xC3, 0x14, 0x0F, 0xAA, 0x19, 0xE8, 0xA2, 0xB9, 0x9B, 0x0C, 0x61, 0xE3, 0x3C, 0x27, 0xEA,
	0x91, 0x39, 0x73, 0xE4, 0x5B, 0x5B, 0xC6, 0xE3, 0x9C, 0x27, 0x0D, 0xAC, 0x53, 0x18,
};

static const uint8_t k_node01_pubkey[] = {
	0x04, 0xBC, 0xF6, 0x58, 0x0D, 0x2D, 0x71, 0xE1, 0x44, 0x16, 0x65, 0x1F, 0x7C,
	0x31, 0x1B, 0x5E, 0xFC, 0xF9, 0xAE, 0xC0, 0xA8, 0xC1, 0x0A, 0xF8, 0x09, 0x27,
	0x84, 0x4C, 0x24, 0x0F, 0x51, 0xA8, 0xEB, 0x23, 0xFA, 0x07, 0x44, 0x13, 0x88,
	0x87, 0xAC, 0x1E, 0x73, 0xCB, 0x72, 0xA0, 0x54, 0xB6, 0xA0, 0xDB, 0x06, 0x22,
	0xAA, 0x80, 0x70, 0x71, 0x01, 0x63, 0x13, 0xB1, 0x59, 0x6C, 0x85, 0x52, 0xCF,
};

void test_matter_fabric(void)
{
	struct matter_cert_info info;
	uint8_t junk[8];

	t_group("a node certificate");

	T_EQ("node01 parses", matter_cert_parse(k_node01, sizeof(k_node01), &info), MATTER_OK);
	T_OK("subject carries a node id", info.have_node_id);
	T_OK("node id is the vector's", info.node_id == UINT64_C(0xDEDEDEDE00010001));
	T_OK("subject carries a fabric id", info.have_fabric_id);
	T_OK("fabric id is the vector's", info.fabric_id == UINT64_C(0xFAB000000000001D));
	T_OK("public key found", info.have_public_key);
	T_OK("public key is uncompressed", info.public_key[0] == 0x04u);
	T_OK("public key is the certificate's",
	     memcmp(info.public_key, k_node01_pubkey, sizeof(k_node01_pubkey)) == 0);

	t_group("a root certificate");

	T_EQ("root01 parses", matter_cert_parse(k_root01, sizeof(k_root01), &info), MATTER_OK);
	T_OK("public key found", info.have_public_key);
	/*
	 * The root's subject is matter-rcac-id, tag 20. Reporting a node id
	 * here would mean the parser is matching on "some DN attribute" rather
	 * than on which one.
	 */
	T_OK("no node id claimed", !info.have_node_id);
	T_OK("no fabric id claimed", !info.have_fabric_id);
	T_OK("node id left zero", info.node_id == 0u);

	t_group("what is not a certificate");

	T_EQ("NULL certificate refused", matter_cert_parse(NULL, 10u, &info), MATTER_E_INVAL);
	T_EQ("NULL output refused", matter_cert_parse(k_root01, sizeof(k_root01), NULL),
	     MATTER_E_INVAL);
	T_EQ("empty refused", matter_cert_parse(k_root01, 0u, &info), MATTER_E_TYPE);
	/* An integer where a structure belongs: 0x04 is an anonymous uint8. */
	junk[0] = 0x04u;
	junk[1] = 0x2Au;
	T_EQ("a bare integer refused", matter_cert_parse(junk, 2u, &info), MATTER_E_TYPE);

	t_group("every truncation of a certificate");
	{
		int accepted = 0;
		int complete = 0;

		for (size_t n = 1u; n < sizeof(k_node01); n++) {
			if (matter_cert_parse(k_node01, n, &info) == MATTER_OK) {
				accepted++;
				if (info.have_node_id && info.have_fabric_id &&
				    info.have_public_key) {
					complete++;
				}
			}
		}
		/* A truncated certificate is a truncated certificate. Accepting
		 * one that happens to hold all three fields would mean a peer
		 * could cut the signature off and still be believed. */
		T_EQ("none accepted", accepted, 0);
		T_EQ("none looked complete", complete, 0);
	}
}

/* ------------------------------------------------------------ AddNOC --- */

/** Wrap a command's arguments the way an InvokeRequest carries them. */
static size_t fields_bytes(uint8_t *buf, size_t cap, uint8_t tag, const uint8_t *v, size_t len)
{
	struct matter_tlv_writer w;
	size_t n = 0u;

	matter_tlv_writer_init(&w, buf, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(1u), MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(tag), v, len);
	(void)matter_tlv_end_container(&w);
	T_EQ("fields encoded", matter_tlv_writer_finish(&w, &n), MATTER_OK);
	return n;
}

/** AddNOC's five arguments, with the ICAC omitted the way Apple omits it. */
static size_t fields_addnoc(uint8_t *buf, size_t cap, const uint8_t *noc, size_t noc_len,
			    const uint8_t *ipk, size_t ipk_len)
{
	struct matter_tlv_writer w;
	size_t n = 0u;

	matter_tlv_writer_init(&w, buf, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(1u), MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(0u), noc, noc_len);
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(2u), ipk, ipk_len);
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(3u), UINT64_C(0x1122334455667788));
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(4u), 0x1349u);
	(void)matter_tlv_end_container(&w);
	T_EQ("fields encoded", matter_tlv_writer_finish(&w, &n), MATTER_OK);
	return n;
}

static void invoke_init(struct matter_im_invoke *inv, uint32_t command, const uint8_t *fields,
			size_t len)
{
	memset(inv, 0, sizeof(*inv));
	inv->endpoint = MATTER_ENDPOINT_ROOT;
	inv->cluster = MATTER_CLUSTER_OPERATIONAL_CREDENTIALS;
	inv->command = command;
	inv->fields = fields;
	inv->fields_len = len;
	inv->has_fields = true;
}

void test_matter_addnoc(void)
{
	struct matter_device_info dev;
	struct matter_im_server srv;
	struct matter_im_invoke inv;
	uint8_t fields[512];
	uint8_t ipk[MATTER_IPK_LEN];
	uint32_t response = 0u;
	size_t len;

	for (size_t i = 0; i < sizeof(ipk); i++) {
		ipk[i] = (uint8_t)(0x30u + i);
	}

	memset(&dev, 0, sizeof(dev));
	matter_clusters_init(&srv, &dev);

	t_group("a root outside a fail-safe");

	len = fields_bytes(fields, sizeof(fields), 0u, k_root01, sizeof(k_root01));
	invoke_init(&inv, MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE, fields, len);
	response = 0u;
	T_EQ("refused", srv.command(srv.ctx, &inv, &response), MATTER_IM_STATUS_FAILSAFE_REQUIRED);
	T_OK("no root installed", !dev.fabric.have_root);

	t_group("a root inside one");

	dev.failsafe_armed = true;
	response = 0u;
	T_EQ("accepted", srv.command(srv.ctx, &inv, &response), MATTER_IM_STATUS_SUCCESS);
	/* AddTrustedRootCertificate has no response command; the reply is a bare
	 * SUCCESS status. */
	T_OK("no response command", response == MATTER_IM_NO_RESPONSE);
	T_OK("root installed", dev.fabric.have_root);
	T_OK("root key is the certificate's", dev.fabric.root_public_key[0] == 0x04u);

	t_group("a root that is not a certificate");
	{
		uint8_t bad[16];
		uint8_t before[MATTER_FABRIC_PUBKEY_LEN];

		memcpy(before, dev.fabric.root_public_key, sizeof(before));
		memset(bad, 0xAA, sizeof(bad));
		len = fields_bytes(fields, sizeof(fields), 0u, bad, sizeof(bad));
		invoke_init(&inv, MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE, fields, len);
		T_EQ("refused", srv.command(srv.ctx, &inv, &response),
		     MATTER_IM_STATUS_INVALID_COMMAND);
		T_OK("the installed root is untouched",
		     memcmp(before, dev.fabric.root_public_key, sizeof(before)) == 0);
		T_OK("and still installed", dev.fabric.have_root);
	}

	t_group("a NOC with no CSR behind it");

	len = fields_addnoc(fields, sizeof(fields), k_node01, sizeof(k_node01), ipk, sizeof(ipk));
	invoke_init(&inv, MATTER_CMD_OC_ADD_NOC, fields, len);
	T_EQ("the command itself succeeds", srv.command(srv.ctx, &inv, &response),
	     MATTER_IM_STATUS_SUCCESS);
	T_OK("answered by NOCResponse", response == MATTER_CMD_OC_NOC_RESPONSE);
	T_EQ("verdict is MissingCsr", dev.last_noc_status, MATTER_NOC_STATUS_MISSING_CSR);
	T_EQ("no fabric created", dev.fabric.index, 0);

	t_group("a NOC certifying somebody else's key");

	dev.have_op_key = true;
	memset(dev.op_pub, 0x04, sizeof(dev.op_pub));
	T_EQ("the command itself succeeds", srv.command(srv.ctx, &inv, &response),
	     MATTER_IM_STATUS_SUCCESS);
	T_EQ("verdict is InvalidPublicKey", dev.last_noc_status,
	     MATTER_NOC_STATUS_INVALID_PUBLIC_KEY);
	T_EQ("no fabric created", dev.fabric.index, 0);

	t_group("a NOC with a short IPK");

	memcpy(dev.op_pub, k_node01_pubkey, sizeof(k_node01_pubkey));
	len = fields_addnoc(fields, sizeof(fields), k_node01, sizeof(k_node01), ipk,
			    sizeof(ipk) - 1u);
	invoke_init(&inv, MATTER_CMD_OC_ADD_NOC, fields, len);
	T_EQ("the command itself succeeds", srv.command(srv.ctx, &inv, &response),
	     MATTER_IM_STATUS_SUCCESS);
	T_EQ("verdict is InvalidNOC", dev.last_noc_status, MATTER_NOC_STATUS_INVALID_NOC);
	T_EQ("no fabric created", dev.fabric.index, 0);

	t_group("the NOC this node asked for");

	len = fields_addnoc(fields, sizeof(fields), k_node01, sizeof(k_node01), ipk, sizeof(ipk));
	invoke_init(&inv, MATTER_CMD_OC_ADD_NOC, fields, len);
	T_EQ("the command itself succeeds", srv.command(srv.ctx, &inv, &response),
	     MATTER_IM_STATUS_SUCCESS);
	T_EQ("verdict is Ok", dev.last_noc_status, MATTER_NOC_STATUS_OK);
	T_EQ("fabric index is 1", dev.fabric.index, 1);
	T_OK("node id taken from the NOC", dev.fabric.node_id == UINT64_C(0xDEDEDEDE00010001));
	T_OK("fabric id taken from the NOC", dev.fabric.fabric_id == UINT64_C(0xFAB000000000001D));
	T_EQ("the NOC is kept whole", dev.fabric.noc_len, sizeof(k_node01));
	T_OK("and kept verbatim", memcmp(dev.fabric.noc, k_node01, sizeof(k_node01)) == 0);
	T_EQ("no ICAC, as Apple sends none", dev.fabric.icac_len, 0);
	T_OK("the IPK is kept", memcmp(dev.fabric.ipk, ipk, sizeof(ipk)) == 0);
	T_OK("the admin subject is kept",
	     dev.fabric.case_admin_subject == UINT64_C(0x1122334455667788));
	T_EQ("the admin vendor is kept", dev.fabric.admin_vendor_id, 0x1349);

	t_group("a second NOC");

	T_EQ("the command itself succeeds", srv.command(srv.ctx, &inv, &response),
	     MATTER_IM_STATUS_SUCCESS);
	T_EQ("verdict is TableFull", dev.last_noc_status, MATTER_NOC_STATUS_TABLE_FULL);
	T_EQ("the first fabric survives", dev.fabric.index, 1);

	t_group("what a commissioner can read back");
	{
		struct matter_tlv_writer w;
		uint8_t buf[16];
		size_t n = 0u;

		T_EQ("SupportedFabrics is answered",
		     srv.status(srv.ctx, MATTER_ENDPOINT_ROOT,
				MATTER_CLUSTER_OPERATIONAL_CREDENTIALS,
				MATTER_ATTR_OC_SUPPORTED_FABRICS),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("NOCs is not",
		     srv.status(srv.ctx, MATTER_ENDPOINT_ROOT,
				MATTER_CLUSTER_OPERATIONAL_CREDENTIALS, 0x0000u),
		     MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE);

		matter_tlv_writer_init(&w, buf, sizeof(buf));
		srv.value(srv.ctx, MATTER_ENDPOINT_ROOT, MATTER_CLUSTER_OPERATIONAL_CREDENTIALS,
			  MATTER_ATTR_OC_COMMISSIONED_FABRICS, &w, MATTER_TLV_CTX(1u));
		T_EQ("CommissionedFabrics encodes", matter_tlv_writer_finish(&w, &n), MATTER_OK);
		/* context tag 1, uint8, value 1 */
		T_EQ("as one fabric", n, 3u);
		T_EQ("and it is one", buf[2], 1u);
	}

	t_group("the NOCResponse on the wire");
	{
		uint8_t out[128];
		size_t n = 0u;

		/*
		 * The encoder RUNS the command before serialising its reply, so
		 * emptying the table here is what makes this AddNOC succeed --
		 * setting last_noc_status would be overwritten a moment later.
		 */
		dev.fabric.index = 0u;
		T_EQ("encodes", matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &n),
		     MATTER_OK);
		T_EQ("and it succeeded", dev.last_noc_status, MATTER_NOC_STATUS_OK);
		/* CommandDataIB [0], path command 0x08, fields {status 0,
		 * fabricIndex 1}. */
		t_vec("NOCResponse, accepted", out, n,
		      "1528003601153500370024000024013e24020818350124000024010118181818"
		      "24ff0c18");

		/* Again, with the table now full: the same response command,
		 * carrying a different verdict and NO fabric index -- an index
		 * for a fabric that was not created is a number a commissioner
		 * could act on. */
		T_EQ("encodes", matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &n),
		     MATTER_OK);
		T_EQ("and it was refused", dev.last_noc_status, MATTER_NOC_STATUS_TABLE_FULL);
		t_vec("NOCResponse, refused", out, n,
		      "1528003601153500370024000024013e2402081835012400051818181824ff0c18");
	}

	t_group("the AddTrustedRootCertificate reply on the wire");
	{
		uint8_t out[128];
		size_t n = 0u;

		len = fields_bytes(fields, sizeof(fields), 0u, k_root01, sizeof(k_root01));
		invoke_init(&inv, MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE, fields, len);
		T_EQ("encodes", matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &n),
		     MATTER_OK);
		/* A CommandStatusIB, not a CommandDataIB: nothing to report but
		 * that it worked. */
		t_vec("status-only reply", out, n,
		      "1528003601153501370024000024013e24020b1835012400001818181824ff0c18");
	}
}
