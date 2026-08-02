/**
 * @file test_matter_clusters.c — the two Aliro commands, the ACL write, and resume.
 *
 * These four entry points are what a commissioner uses to turn a blank node into a
 * working reader, and none of them were reachable from the other suites: the Aliro
 * pair are static command handlers behind matter_im_server::command, the ACL write is
 * behind ::write, and matter_clusters_resume() only runs on a reboot with a stored
 * dataset. All four are driven here through the server vtable that
 * matter_clusters_init() fills in, which is the same seam the IM layer calls in
 * production -- no test-only entry point exists or is added.
 *
 * What the assertions are worth: every status code is checked against the branch that
 * produces it, and the failure cases matter more than the success ones. A length-
 * tolerant SetAliroReaderConfig would store a 64-byte "P-256 public key" and the
 * reader would fail later, somewhere else, for a reason nobody would connect back to
 * commissioning. So each length is wrong by exactly one byte in its own case.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */

#include "test.h"
#include "test_matter_thread_stub.h"

#include "matter_clusters.h"
#include "matter_im.h"
#include "matter_tlv.h"

#include <string.h>

/* ---- callback doubles ---------------------------------------------------- */

static int s_cfg_calls;
static int s_cfg_result;
static uint8_t s_cfg_signing[MATTER_ALIRO_SIGNING_KEY_LEN];
static uint8_t s_cfg_verification[MATTER_ALIRO_VERIFICATION_KEY_LEN];
static uint8_t s_cfg_group_id[MATTER_ALIRO_GROUP_ID_LEN];
static uint8_t s_cfg_grk[MATTER_ALIRO_GROUP_ID_LEN];

static int cfg_cb(const uint8_t signing[MATTER_ALIRO_SIGNING_KEY_LEN],
		  const uint8_t verification[MATTER_ALIRO_VERIFICATION_KEY_LEN],
		  const uint8_t group_id[MATTER_ALIRO_GROUP_ID_LEN],
		  const uint8_t grk[MATTER_ALIRO_GROUP_ID_LEN])
{
	s_cfg_calls++;
	memcpy(s_cfg_signing, signing, sizeof(s_cfg_signing));
	memcpy(s_cfg_verification, verification, sizeof(s_cfg_verification));
	memcpy(s_cfg_group_id, group_id, sizeof(s_cfg_group_id));
	memcpy(s_cfg_grk, grk, sizeof(s_cfg_grk));
	return s_cfg_result;
}

static int s_cred_calls;
static int s_cred_result;
static uint8_t s_cred_type;
static uint8_t s_cred_key[MATTER_ALIRO_VERIFICATION_KEY_LEN];

static int cred_cb(uint8_t credential_type, const uint8_t public_key[MATTER_ALIRO_VERIFICATION_KEY_LEN])
{
	s_cred_calls++;
	s_cred_type = credential_type;
	memcpy(s_cred_key, public_key, sizeof(s_cred_key));
	return s_cred_result;
}

/* ---- fixtures ------------------------------------------------------------ */

static void reset_doubles(void)
{
	s_cfg_calls = 0;
	s_cfg_result = 0;
	s_cred_calls = 0;
	s_cred_result = 0;
	s_cred_type = 0xFFu;
	memset(s_cfg_signing, 0, sizeof(s_cfg_signing));
	memset(s_cfg_verification, 0, sizeof(s_cfg_verification));
	memset(s_cfg_group_id, 0, sizeof(s_cfg_group_id));
	memset(s_cfg_grk, 0, sizeof(s_cfg_grk));
	memset(s_cred_key, 0, sizeof(s_cred_key));
}

static void fill_info(struct matter_device_info *info)
{
	memset(info, 0, sizeof(*info));
	info->vendor_id = 0xFFF1u;
	info->product_id = 0x8001u;
	info->regulatory_config = MATTER_REGULATORY_INDOOR;
	info->location_capability = MATTER_REGULATORY_INDOOR;
	info->failsafe_expiry_s = 60u;
	info->failsafe_max_s = 900u;
	info->supports_concurrent_connection = true;
	info->aliro_reader_config_cb = cfg_cb;
	info->aliro_credential_cb = cred_cb;
}

/* A byte pattern that differs per field, so a handler that mixes two of them up
 * fails rather than passing on two buffers that happen to match. */
static void pattern(uint8_t *dst, size_t len, uint8_t seed)
{
	size_t i;

	for (i = 0u; i < len; i++) {
		dst[i] = (uint8_t)(seed + (uint8_t)i);
	}
}

/**
 * SetAliroReaderConfig arguments. Any length may be passed, including a wrong
 * one -- that is the point of most of the cases below. @p grk_len of SIZE_MAX
 * omits the GroupResolvingKey field entirely.
 */
static size_t build_cfg_fields(uint8_t *buf, size_t cap, const uint8_t *signing, size_t signing_len,
			       const uint8_t *verification, size_t verification_len,
			       const uint8_t *group_id, size_t group_id_len, const uint8_t *grk,
			       size_t grk_len)
{
	struct matter_tlv_writer w;
	size_t len = 0u;

	matter_tlv_writer_init(&w, buf, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	if (signing != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_ALIRO_CFG_SIGNING_KEY), signing,
					   signing_len);
	}
	if (verification != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_ALIRO_CFG_VERIFICATION_KEY),
					   verification, verification_len);
	}
	if (group_id != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_ALIRO_CFG_GROUP_ID), group_id,
					   group_id_len);
	}
	if (grk != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_ALIRO_CFG_GROUP_RESOLVING_KEY),
					   grk, grk_len);
	}
	(void)matter_tlv_end_container(&w);
	(void)matter_tlv_writer_finish(&w, &len);
	return len;
}

/** SetCredential arguments. @p have_user_index omits field 3 when false. */
static size_t build_cred_fields(uint8_t *buf, size_t cap, uint64_t cred_type, bool have_cred_struct,
				const uint8_t *data, size_t data_len, bool have_user_index,
				uint64_t user_index)
{
	struct matter_tlv_writer w;
	size_t len = 0u;

	matter_tlv_writer_init(&w, buf, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	if (have_cred_struct) {
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_SETCRED_CREDENTIAL),
						 MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_CREDSTRUCT_TYPE), cred_type);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_CREDSTRUCT_INDEX), 1u);
		(void)matter_tlv_end_container(&w);
	}
	if (data != NULL) {
		(void)matter_tlv_put_bytes(&w, MATTER_TLV_CTX(TAG_SETCRED_DATA), data, data_len);
	}
	if (have_user_index) {
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETCRED_USER_INDEX), user_index);
	}
	(void)matter_tlv_end_container(&w);
	(void)matter_tlv_writer_finish(&w, &len);
	return len;
}

static uint8_t run_command(struct matter_im_server *srv, uint32_t cluster, uint32_t cmd,
			   const uint8_t *fields, size_t fields_len, uint32_t *response_command)
{
	struct matter_im_invoke inv;
	uint32_t rc = 0u;

	memset(&inv, 0, sizeof(inv));
	inv.endpoint = MATTER_ENDPOINT_LOCK;
	inv.cluster = cluster;
	inv.command = cmd;
	inv.fields = fields;
	inv.fields_len = fields_len;
	inv.has_fields = fields != NULL;
	return srv->command(srv->ctx, &inv, response_command != NULL ? response_command : &rc);
}

/* ---- suite --------------------------------------------------------------- */

void test_matter_clusters(void)
{
	struct matter_device_info info;
	struct matter_im_server srv;
	uint8_t signing[MATTER_ALIRO_SIGNING_KEY_LEN];
	uint8_t verification[MATTER_ALIRO_VERIFICATION_KEY_LEN];
	uint8_t group_id[MATTER_ALIRO_GROUP_ID_LEN];
	uint8_t grk[MATTER_ALIRO_GROUP_ID_LEN];
	uint8_t fields[256];
	size_t flen;

	pattern(signing, sizeof(signing), 0x10u);
	pattern(verification, sizeof(verification), 0x40u);
	pattern(group_id, sizeof(group_id), 0x80u);
	pattern(grk, sizeof(grk), 0xC0u);

	t_group("SetAliroReaderConfig accepts a well-formed identity");
	{
		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), group_id,
					sizeof(group_id), grk, sizeof(grk));
		T_OK("fields encode", flen > 0u);
		T_EQ("accepted",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("store written once", s_cfg_calls, 1);
		T_OK("signing key forwarded verbatim",
		     memcmp(s_cfg_signing, signing, sizeof(signing)) == 0);
		T_OK("verification key forwarded verbatim",
		     memcmp(s_cfg_verification, verification, sizeof(verification)) == 0);
		T_OK("group id forwarded verbatim",
		     memcmp(s_cfg_group_id, group_id, sizeof(group_id)) == 0);
		T_OK("resolving key forwarded verbatim",
		     memcmp(s_cfg_grk, grk, sizeof(grk)) == 0);

		/* The signing key is the one field NOT mirrored into device state:
		 * it goes to the store and nowhere else. */
		T_OK("config marked present", info.have_aliro_reader_config);
		T_OK("resolving key marked present", info.have_aliro_group_resolving_key);
		T_OK("verification key mirrored",
		     memcmp(info.aliro_verification_key, verification, sizeof(verification)) == 0);
		T_OK("group id mirrored",
		     memcmp(info.aliro_group_id, group_id, sizeof(group_id)) == 0);
		T_OK("resolving key mirrored",
		     memcmp(info.aliro_group_resolving_key, grk, sizeof(grk)) == 0);
	}

	t_group("SetAliroReaderConfig refuses a malformed identity");
	{
		/* Each length wrong by exactly one byte, one field at a time. */
		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);

		flen = build_cfg_fields(fields, sizeof(fields), NULL, 0u, verification,
					sizeof(verification), group_id, sizeof(group_id), grk,
					sizeof(grk));
		T_EQ("no signing key is an invalid command",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing), NULL, 0u,
					group_id, sizeof(group_id), grk, sizeof(grk));
		T_EQ("no verification key is an invalid command",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), NULL, 0u, grk,
					sizeof(grk));
		T_EQ("no group id is an invalid command",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing) - 1u,
					verification, sizeof(verification), group_id,
					sizeof(group_id), grk, sizeof(grk));
		T_EQ("a 31-byte signing key is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification) - 1u, group_id,
					sizeof(group_id), grk, sizeof(grk));
		T_EQ("a 64-byte verification key is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), group_id,
					sizeof(group_id) - 1u, grk, sizeof(grk));
		T_EQ("a 15-byte group id is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), group_id,
					sizeof(group_id), NULL, 0u);
		T_EQ("a missing resolving key is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), group_id,
					sizeof(group_id), grk, sizeof(grk) - 1u);
		T_EQ("a 15-byte resolving key is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);

		T_EQ("nothing reached the store", s_cfg_calls, 0);
		T_OK("and no config was recorded", !info.have_aliro_reader_config);
	}

	t_group("SetAliroReaderConfig fails loudly when the store cannot keep it");
	{
		reset_doubles();
		fill_info(&info);
		info.aliro_reader_config_cb = NULL;
		matter_clusters_init(&srv, &info);

		flen = build_cfg_fields(fields, sizeof(fields), signing, sizeof(signing),
					verification, sizeof(verification), group_id,
					sizeof(group_id), grk, sizeof(grk));
		/* Success here would claim an identity survives a reboot that will be
		 * gone at the next boot. */
		T_EQ("no store is a failure, not a success",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_FAILURE);
		T_OK("and no config was recorded", !info.have_aliro_reader_config);

		reset_doubles();
		fill_info(&info);
		s_cfg_result = -1;
		matter_clusters_init(&srv, &info);
		T_EQ("a refusing store is a failure",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
				 MATTER_CMD_DL_SET_ALIRO_READER_CONFIG, fields, flen, NULL),
		     MATTER_IM_STATUS_FAILURE);
		T_EQ("the store was asked", s_cfg_calls, 1);
		T_OK("and nothing was mirrored", !info.have_aliro_reader_config);
	}

	t_group("SetCredential installs the three Aliro credential types");
	{
		const uint8_t types[3] = { MATTER_DL_CRED_ALIRO_ISSUER_KEY,
					   MATTER_DL_CRED_ALIRO_EVICTABLE_ENDPOINT,
					   MATTER_DL_CRED_ALIRO_ENDPOINT_KEY };
		size_t i;

		for (i = 0u; i < 3u; i++) {
			uint32_t resp = 0u;

			reset_doubles();
			fill_info(&info);
			matter_clusters_init(&srv, &info);

			flen = build_cred_fields(fields, sizeof(fields), types[i], true, verification,
						 sizeof(verification), true, 7u);
			T_EQ("command succeeds",
			     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK,
					 MATTER_CMD_DL_SET_CREDENTIAL, fields, flen, &resp),
			     MATTER_IM_STATUS_SUCCESS);
			T_EQ("answered with SetCredentialResponse", (long)resp,
			     (long)MATTER_CMD_DL_SET_CREDENTIAL_RESPONSE);
			T_EQ("credential status SUCCESS", info.last_credential_status,
			     MATTER_IM_STATUS_SUCCESS);
			T_EQ("type forwarded", s_cred_type, types[i]);
			T_EQ("installed once", s_cred_calls, 1);
			T_OK("key forwarded verbatim",
			     memcmp(s_cred_key, verification, sizeof(verification)) == 0);
			T_EQ("user index recorded", info.last_user_index, 7u);
		}
	}

	t_group("SetCredential refuses what the reader cannot use");
	{
		uint32_t resp = 0u;

		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);

		/* PIN: a surface this node does not claim. */
		flen = build_cred_fields(fields, sizeof(fields), 1u, true, verification,
					 sizeof(verification), true, 1u);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, &resp),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("but the credential is unsupported", info.last_credential_status,
		     MATTER_IM_STATUS_UNSUPPORTED_COMMAND);
		T_EQ("and nothing was installed", s_cred_calls, 0);

		flen = build_cred_fields(fields, sizeof(fields), MATTER_DL_CRED_ALIRO_ISSUER_KEY,
					 true, verification, sizeof(verification) - 1u, true, 1u);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, &resp),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("a 64-byte key is a constraint error", info.last_credential_status,
		     MATTER_IM_STATUS_CONSTRAINT_ERROR);
		T_EQ("and nothing was installed", s_cred_calls, 0);

		flen = build_cred_fields(fields, sizeof(fields), 0u, false, verification,
					 sizeof(verification), true, 1u);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, &resp),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("no CredentialStruct is an invalid command", info.last_credential_status,
		     MATTER_IM_STATUS_INVALID_COMMAND);

		flen = build_cred_fields(fields, sizeof(fields), MATTER_DL_CRED_ALIRO_ISSUER_KEY,
					 true, NULL, 0u, true, 1u);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, &resp),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("no CredentialData is an invalid command", info.last_credential_status,
		     MATTER_IM_STATUS_INVALID_COMMAND);
		T_EQ("and nothing was installed", s_cred_calls, 0);
	}

	t_group("SetCredential reports a store that refused");
	{
		reset_doubles();
		fill_info(&info);
		info.aliro_credential_cb = NULL;
		matter_clusters_init(&srv, &info);

		flen = build_cred_fields(fields, sizeof(fields), MATTER_DL_CRED_ALIRO_ISSUER_KEY,
					 true, verification, sizeof(verification), true, 3u);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("no store leaves the status at FAILURE", info.last_credential_status,
		     MATTER_IM_STATUS_FAILURE);
		T_EQ("and no user index is claimed", info.last_user_index, 0u);

		reset_doubles();
		fill_info(&info);
		s_cred_result = -1;
		matter_clusters_init(&srv, &info);
		T_EQ("command still succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("a refusing store leaves FAILURE", info.last_credential_status,
		     MATTER_IM_STATUS_FAILURE);
		T_EQ("the store was asked", s_cred_calls, 1);

		/* UserIndex is optional: omitting it must not fail the install. */
		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);
		flen = build_cred_fields(fields, sizeof(fields), MATTER_DL_CRED_ALIRO_ISSUER_KEY,
					 true, verification, sizeof(verification), false, 0u);
		T_EQ("command succeeds",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_CREDENTIAL,
				 fields, flen, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("installed without a user index", info.last_credential_status,
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("index stays zero", info.last_user_index, 0u);
	}

	t_group("the ACL is the only writable attribute");
	{
		struct matter_im_path path;
		uint8_t acl[64];
		uint8_t oversized[MATTER_ACL_MAX + 1u];

		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);
		pattern(acl, sizeof(acl), 0x20u);
		memset(oversized, 0xAAu, sizeof(oversized));

		memset(&path, 0, sizeof(path));
		path.endpoint = MATTER_ENDPOINT_ROOT;
		path.cluster = MATTER_CLUSTER_ACCESS_CONTROL;
		path.attribute = MATTER_ATTR_AC_ACL;
		T_EQ("ACL write accepted", srv.write(srv.ctx, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("length recorded", info.acl_len, sizeof(acl));
		T_OK("bytes stored verbatim", memcmp(info.acl, acl, sizeof(acl)) == 0);

		/* Truncating would read back as a shorter list than was written, which
		 * looks like the node silently dropped entries it was asked to grant. */
		T_EQ("an oversized ACL is refused, not truncated",
		     srv.write(srv.ctx, &path, oversized, sizeof(oversized)),
		     MATTER_IM_STATUS_RESOURCE_EXHAUSTED);
		T_EQ("and the stored one is untouched", info.acl_len, sizeof(acl));

		T_EQ("an empty write is an invalid command", srv.write(srv.ctx, &path, acl, 0u),
		     MATTER_IM_STATUS_INVALID_COMMAND);
		T_EQ("a null write is an invalid command",
		     srv.write(srv.ctx, &path, NULL, sizeof(acl)),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		path.attribute = MATTER_ATTR_AC_ACL + 1u;
		T_EQ("another Access Control attribute is not writable",
		     srv.write(srv.ctx, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_UNSUPPORTED_WRITE);

		/* A cluster this node really has, and a cluster it does not: the two
		 * answers must differ, or a commissioner cannot tell a typo from a
		 * read-only attribute. */
		path.cluster = MATTER_CLUSTER_DESCRIPTOR;
		path.attribute = 0u;
		T_EQ("a real read-only cluster says UNSUPPORTED_WRITE",
		     srv.write(srv.ctx, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_UNSUPPORTED_WRITE);

		path.cluster = 0x1234u;
		T_EQ("a cluster this node lacks says UNSUPPORTED_CLUSTER",
		     srv.write(srv.ctx, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_UNSUPPORTED_CLUSTER);

		path.endpoint = 9u;
		path.cluster = MATTER_CLUSTER_ACCESS_CONTROL;
		path.attribute = MATTER_ATTR_AC_ACL;
		T_EQ("an endpoint this node lacks says UNSUPPORTED_ENDPOINT",
		     srv.write(srv.ctx, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT);

		path.endpoint = MATTER_ENDPOINT_ROOT;
		T_EQ("a null device refuses every write", srv.write(NULL, &path, acl, sizeof(acl)),
		     MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT);
	}

	t_group("the lock endpoint answers its own commands");
	{
		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);

		/* The tile's two buttons, answered with a bare status: DoorLock
		 * defines no LockDoorResponse, and inventing one leaves the
		 * controller waiting. */
		T_EQ("UnlockDoor accepted",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_UNLOCK_DOOR, NULL,
				 0u, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("and the lock reports unlocked", info.lock_state,
		     MATTER_DL_LOCK_STATE_UNLOCKED);
		T_EQ("LockDoor accepted",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_LOCK_DOOR, NULL, 0u,
				 NULL),
		     MATTER_IM_STATUS_SUCCESS);
		T_EQ("and the lock reports locked", info.lock_state, MATTER_DL_LOCK_STATE_LOCKED);

		/* A cluster the lock endpoint does not have. Answering
		 * UNSUPPORTED_ENDPOINT here would contradict every attribute read
		 * that says endpoint 1 exists. */
		T_EQ("a cluster this endpoint lacks says UNSUPPORTED_CLUSTER",
		     run_command(&srv, MATTER_CLUSTER_DESCRIPTOR, MATTER_CMD_DL_LOCK_DOOR, NULL, 0u,
				 NULL),
		     MATTER_IM_STATUS_UNSUPPORTED_CLUSTER);

		/* A Door Lock command this node has not implemented: the endpoint and
		 * the cluster are both real, the command is not. */
		T_EQ("an unimplemented command says UNSUPPORTED_COMMAND",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, 0x00FFu, NULL, 0u, NULL),
		     MATTER_IM_STATUS_UNSUPPORTED_COMMAND);
	}

	t_group("SetUser stores what GetUser will be asked for");
	{
		struct matter_tlv_writer w;

		reset_doubles();
		fill_info(&info);
		info.accessing_fabric_index = 2u;
		matter_clusters_init(&srv, &info);

		matter_tlv_writer_init(&w, fields, sizeof(fields));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_INDEX), 1u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_UNIQUE_ID), 0xABCDu);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_STATUS), 1u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_TYPE), 0u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_CREDENTIAL_RULE), 0u);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_writer_finish(&w, &flen);

		T_EQ("accepted",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_USER, fields,
				 flen, NULL),
		     MATTER_IM_STATUS_SUCCESS);
		/* Stored, not merely accepted: Apple reads the user straight back, and
		 * a node that says SUCCESS then reports an empty slot has told the
		 * controller two different things about the same user. */
		T_OK("slot 1 is in use", info.users[0].in_use);
		T_EQ("unique id kept", (long)info.users[0].unique_id, 0xABCDL);
		T_EQ("creator fabric recorded", info.users[0].creator_fabric, 2u);

		/* Index 0 and one past the table are both out of range. */
		matter_tlv_writer_init(&w, fields, sizeof(fields));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_INDEX), 0u);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_writer_finish(&w, &flen);
		T_EQ("index 0 is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_USER, fields,
				 flen, NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		matter_tlv_writer_init(&w, fields, sizeof(fields));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_SETUSER_INDEX),
					 MATTER_DL_USERS_MAX + 1u);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_writer_finish(&w, &flen);
		T_EQ("one past the table is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_USER, fields,
				 flen, NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);

		T_EQ("no index at all is refused",
		     run_command(&srv, MATTER_CLUSTER_DOOR_LOCK, MATTER_CMD_DL_SET_USER, NULL, 0u,
				 NULL),
		     MATTER_IM_STATUS_INVALID_COMMAND);
	}

	t_group("resume brings Thread back without a commissioner");
	{
		reset_doubles();
		fill_info(&info);
		matter_clusters_init(&srv, &info);

		T_EQ("a null device cannot resume", matter_clusters_resume(NULL), MATTER_E_STATE);
		T_EQ("no stored dataset means nothing to resume",
		     matter_clusters_resume(&info), MATTER_E_STATE);
		T_OK("and Thread was never started", !info.thread_started);

		/* A dataset that the stack accepts. */
		test_matter_thread_stub_reset();
		info.thread_dataset_len = 16u;
		memset(info.thread_dataset, 0x5Au, info.thread_dataset_len);
		T_EQ("resume starts Thread", matter_clusters_resume(&info), MATTER_OK);
		T_OK("and records that it did", info.thread_started);
		T_EQ("the stack was started once", g_thread_start_calls, 1);
		T_EQ("with the stored dataset", (long)g_thread_last_len, 16L);
		T_OK("verbatim",
		     memcmp(g_thread_last_dataset, info.thread_dataset, 16u) == 0);

		/* A stack that refuses the dataset must not leave the node believing
		 * it is on the network. */
		test_matter_thread_stub_reset();
		info.thread_started = false;
		g_thread_start_fail = 1;
		T_EQ("a refused dataset fails the resume", matter_clusters_resume(&info),
		     MATTER_E_STATE);
		T_OK("and Thread is not claimed to be up", !info.thread_started);
		g_thread_start_fail = 0;
	}
}
