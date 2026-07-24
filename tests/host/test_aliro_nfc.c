#include "test.h"

#include "nfc_select.h"
#include "nfc_auth.h"
#include "nfc_step_up.h"
#include "access_document.h"
#include "tlv.h"

#include <stdint.h>
#include <string.h>

static size_t decode_hex(const char *hex, uint8_t *output, size_t capacity)
{
	size_t length = 0;
	while (hex[0] != '\0' && hex[1] != '\0' && length < capacity) {
		uint8_t value = 0;
		for (size_t i = 0; i < 2; ++i) {
			const char c = hex[i];
			value = (uint8_t)(value << 4);
			value |= (uint8_t)(c >= '0' && c <= '9' ? c - '0' : 10 + c - 'a');
		}
		output[length++] = value;
		hex += 2;
	}
	return length;
}

void test_aliro_nfc(void)
{
	static const uint8_t expected_select[] = {
		0x00, 0xa4, 0x04, 0x00, 0x09, 0xa0, 0x00, 0x00,
		0x09, 0x09, 0xac, 0xce, 0x55, 0x01, 0x00,
	};
	uint8_t command[WOZ_ALIRO_SELECT_COMMAND_SIZE];
	T_EQ("build expedited SELECT",
	     woz_aliro_build_select_command(WOZ_ALIRO_SELECT_EXPEDITED, command), WOZ_ALIRO_SELECT_OK);
	T_OK("expedited SELECT bytes", memcmp(command, expected_select, sizeof(command)) == 0);

	/* Published expedited-standard flow example: FCI(AID, proprietary(type,
	 * versions)) followed by success status. */
	static const uint8_t response[] = {
		0x6f, 0x15, 0x84, 0x09, 0xa0, 0x00, 0x00, 0x09, 0x09, 0xac,
		0xce, 0x55, 0x01, 0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c,
		0x02, 0x01, 0x00, 0x90, 0x00,
	};
	uint16_t version = 0;
	T_EQ("parse SELECT example",
	     woz_aliro_parse_select_response(response, sizeof(response), WOZ_ALIRO_SELECT_EXPEDITED, &version),
	     WOZ_ALIRO_SELECT_OK);
	T_EQ("select protocol 1.0", version, 0x0100);

	uint8_t malformed[sizeof(response)];
	memcpy(malformed, response, sizeof(response));
	malformed[sizeof(malformed) - 1] = 0x01;
	T_EQ("reject unsuccessful status",
	     woz_aliro_parse_select_response(malformed, sizeof(malformed), WOZ_ALIRO_SELECT_EXPEDITED, &version),
	     WOZ_ALIRO_SELECT_STATUS_ERROR);

	memcpy(malformed, response, sizeof(response));
	malformed[12] = 0x02;
	T_EQ("reject wrong selected AID",
	     woz_aliro_parse_select_response(malformed, sizeof(malformed), WOZ_ALIRO_SELECT_EXPEDITED, &version),
	     WOZ_ALIRO_SELECT_WRONG_APPLICATION);

	memcpy(malformed, response, sizeof(response));
	malformed[21] = 0x02;
	T_EQ("reject unsupported version",
	     woz_aliro_parse_select_response(malformed, sizeof(malformed), WOZ_ALIRO_SELECT_EXPEDITED, &version),
	     WOZ_ALIRO_SELECT_VERSION_NOT_SUPPORTED);

	static const uint8_t long_form[] = { 0xb3, 0x81, 0x80 };
	struct woz_aliro_tlv tlv;
	size_t offset = 0;
	T_EQ("reject truncated long TLV",
	     woz_aliro_tlv_next(long_form, sizeof(long_form), &offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t non_minimal[] = { 0x80, 0x81, 0x01, 0x00 };
	offset = 0;
	T_EQ("reject non-minimal DER length",
	     woz_aliro_tlv_next(non_minimal, sizeof(non_minimal), &offset, &tlv), WOZ_ALIRO_TLV_INVALID);

	/* Section 14.3 published expedited-standard AUTH0 transcript. */
	uint8_t reader_ephemeral[WOZ_ALIRO_PUBLIC_KEY_SIZE];
	uint8_t transaction_id[WOZ_ALIRO_TRANSACTION_ID_SIZE];
	uint8_t reader_id[WOZ_ALIRO_READER_ID_SIZE];
	decode_hex("049696afe33de58b7d3253d1cba86d14147c16d455e8a27373b38d454af21b70"
		   "e75e13ebc6d55743ba6a6ffc4ed37a55515a9346fdae311f60be30421fa6dc61c5",
		   reader_ephemeral, sizeof(reader_ephemeral));
	decode_hex("4165a83667ad0af5ab115247424822e0", transaction_id, sizeof(transaction_id));
	decode_hex("00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
		   reader_id, sizeof(reader_id));
	struct woz_aliro_auth0_command auth0 = {
		.command_parameters = 0,
		.authentication_policy = 1,
		.protocol_version = 0x0100,
		.reader_ephemeral_public_key = reader_ephemeral,
		.transaction_identifier = transaction_id,
		.reader_identifier = reader_id,
	};
	uint8_t auth0_command[160];
	size_t auth0_command_length = 0;
	T_EQ("build AUTH0", woz_aliro_build_auth0_command(&auth0, auth0_command,
							 sizeof(auth0_command), &auth0_command_length),
	     WOZ_ALIRO_AUTH_OK);
	uint8_t expected_auth0[WOZ_ALIRO_AUTH0_STANDARD_COMMAND_SIZE];
	const size_t expected_auth0_length = decode_hex(
		"80800000814101004201015c0201008741049696afe33de58b7d3253d1cba86d14147c16d455"
		"e8a27373b38d454af21b70e75e13ebc6d55743ba6a6ffc4ed37a55515a9346fdae311f60be30"
		"421fa6dc61c54c104165a83667ad0af5ab115247424822e04d2000112233445566778899aabbcc"
		"ddeeffffeeddccbbaa9988776655443322110000",
		expected_auth0, sizeof(expected_auth0));
	T_EQ("AUTH0 vector length", auth0_command_length, expected_auth0_length);
	T_OK("AUTH0 vector bytes",
	     memcmp(auth0_command, expected_auth0, expected_auth0_length) == 0);

	uint8_t auth0_response_bytes[160];
	const size_t auth0_response_length = decode_hex(
		"8641045d75ab60136a2c54ff27b799ee157f3f3329435c0df608de904c920ac29f72bd4274c2"
		"edc810a93e240bf5d6394a92c9766b690b2bf5128ae70d6e29257ea7869000",
		auth0_response_bytes, sizeof(auth0_response_bytes));
	struct woz_aliro_auth0_response auth0_response;
	T_EQ("parse AUTH0 standard response",
	     woz_aliro_parse_auth0_response(auth0_response_bytes, auth0_response_length, 0,
					     &auth0_response), WOZ_ALIRO_AUTH_OK);
	T_OK("AUTH0 response public key",
	     auth0_response.credential_ephemeral_public_key[0] == 0x04 &&
		     auth0_response.cryptogram == NULL);
	T_EQ("reject missing fast cryptogram",
	     woz_aliro_parse_auth0_response(auth0_response_bytes, auth0_response_length, 1,
					     &auth0_response), WOZ_ALIRO_AUTH_WRONG_CONTENT);

	const size_t fast_response_length = decode_hex(
		"864104507806c74a52a8e9b34d0796e4e2382ab6f9d9d7417179fc338429bda1c2fff92852d5"
		"c7f5643f1f24e468a6d998effeea81d23c9857d10040c2ea150abede899d40ba76234a1e427f"
		"9e463106251fb9e9edc5f5812f59fd887d4e57eb0bc544b7cb9d368c4dedadf782d520a91f966"
		"6b9091e0973894522c04b142f6447b596942a9000",
		auth0_response_bytes, sizeof(auth0_response_bytes));
	T_EQ("parse AUTH0 fast response",
	     woz_aliro_parse_auth0_response(auth0_response_bytes, fast_response_length, 1,
					     &auth0_response), WOZ_ALIRO_AUTH_OK);
	T_EQ("AUTH0 fast cryptogram length", auth0_response.cryptogram_length, 64);
	T_EQ("reject unexpected fast cryptogram",
	     woz_aliro_parse_auth0_response(auth0_response_bytes, fast_response_length, 0,
					     &auth0_response), WOZ_ALIRO_AUTH_WRONG_CONTENT);
	const size_t restored_standard_length = decode_hex(
		"8641045d75ab60136a2c54ff27b799ee157f3f3329435c0df608de904c920ac29f72bd4274c2"
		"edc810a93e240bf5d6394a92c9766b690b2bf5128ae70d6e29257ea7869000",
		auth0_response_bytes, sizeof(auth0_response_bytes));
	T_EQ("restore AUTH0 standard response",
	     woz_aliro_parse_auth0_response(auth0_response_bytes, restored_standard_length, 0,
					     &auth0_response), WOZ_ALIRO_AUTH_OK);

	uint8_t authentication_data[WOZ_ALIRO_AUTH_DATA_SIZE];
	T_EQ("build AUTH1 authentication data",
	     woz_aliro_build_authentication_data(reader_id,
					       auth0_response.credential_ephemeral_public_key,
					       reader_ephemeral, transaction_id, 0x415d9569,
					       authentication_data), WOZ_ALIRO_AUTH_OK);
	uint8_t expected_authentication_data[WOZ_ALIRO_AUTH_DATA_SIZE];
	const size_t expected_authentication_data_length = decode_hex(
		"4d2000112233445566778899aabbccddeeffffeeddccbbaa9988776655443322110086205d75ab"
		"60136a2c54ff27b799ee157f3f3329435c0df608de904c920ac29f72bd87209696afe33de58b7"
		"d3253d1cba86d14147c16d455e8a27373b38d454af21b70e74c104165a83667ad0af5ab115247"
		"424822e09304415d9569",
		expected_authentication_data, sizeof(expected_authentication_data));
	T_EQ("AUTH1 auth data vector length", expected_authentication_data_length,
	     sizeof(expected_authentication_data));
	T_OK("AUTH1 auth data vector bytes",
	     memcmp(authentication_data, expected_authentication_data,
		    sizeof(authentication_data)) == 0);

	uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE];
	decode_hex("501952e25339019804a7c3a7e4a1f6d993aec8baba7db6c8c20ac450428c2ff3"
		   "90c2188854ef7964927f88040dddf895ef57cce72379ad9688f36c5c7de3c294",
		   signature, sizeof(signature));
	uint8_t auth1_command[WOZ_ALIRO_AUTH1_COMMAND_SIZE];
	size_t auth1_command_length = 0;
	T_EQ("build AUTH1 command", woz_aliro_build_auth1_command(1, signature, auth1_command,
							       sizeof(auth1_command),
							       &auth1_command_length),
	     WOZ_ALIRO_AUTH_OK);
	uint8_t expected_auth1[WOZ_ALIRO_AUTH1_COMMAND_SIZE];
	const size_t expected_auth1_length = decode_hex(
		"80810000454101019e40501952e25339019804a7c3a7e4a1f6d993aec8baba7db6c8c20ac450"
		"428c2ff390c2188854ef7964927f88040dddf895ef57cce72379ad9688f36c5c7de3c29400",
		expected_auth1, sizeof(expected_auth1));
	T_EQ("AUTH1 vector length", auth1_command_length, expected_auth1_length);
	T_OK("AUTH1 vector bytes", memcmp(auth1_command, expected_auth1, expected_auth1_length) == 0);

	uint8_t auth1_plaintext[160];
	const size_t auth1_plaintext_length = decode_hex(
		"5a410488f6f8f2f1e35a58879e72d9ea81957e8964c3d3c566eb9d41c83d0d8c63a23075dbdc"
		"f67d15bda429db38706a2f15ba90a2ac3c6a00973d21ed758c1471a7489e402f57a5cb8a88c5"
		"a300fadb858d17298ed6f9dc01f9abc65e4b4089439868b8d24e93f1e54ca1df0703a76974a84"
		"7ebafb42a7e90dccc3aaed788251d155a63e05e02003f",
		auth1_plaintext, sizeof(auth1_plaintext));
	struct woz_aliro_auth1_response auth1_response;
	T_EQ("parse AUTH1 plaintext",
	     woz_aliro_parse_auth1_plaintext(auth1_plaintext, auth1_plaintext_length, 1,
					     &auth1_response), WOZ_ALIRO_AUTH_OK);
	T_EQ("AUTH1 signaling bitmap", auth1_response.signaling_bitmap, 0x003f);
	T_OK("AUTH1 public key parsed", auth1_response.credential_public_key[0] == 0x04);

	/* Section 14.6 step-up DeviceRequest vector. */
	uint8_t device_request[256];
	size_t device_request_length = 0;
	T_EQ("build step-up DeviceRequest",
	     woz_aliro_build_device_request((const uint8_t *)"element2", 8, true,
					    device_request, sizeof(device_request),
					    &device_request_length), WOZ_ALIRO_STEP_UP_OK);
	uint8_t expected_request[128];
	const size_t expected_request_length = decode_hex(
		"a2613163312e30613281a16131d8185821a26131a167616c69726f2d61a168656c656d656e7432f5613567616c69726f2d61",
		expected_request, sizeof(expected_request));
	T_EQ("DeviceRequest vector length", device_request_length, expected_request_length);
	t_vec("DeviceRequest vector bytes", device_request, device_request_length,
	      "a2613163312e30613281a16131d8185821a26131a167616c69726f2d61a168656c656d656e7432f5613567616c69726f2d61");

	uint8_t session_data[300];
	size_t session_data_length = 0;
	T_EQ("wrap SessionData", woz_aliro_wrap_session_data(device_request,
		device_request_length, session_data, sizeof(session_data), &session_data_length), 0);
	const uint8_t *unwrapped = NULL;
	size_t unwrapped_length = 0;
	T_EQ("unwrap SessionData", woz_aliro_unwrap_session_data(session_data,
		session_data_length, &unwrapped, &unwrapped_length), 0);
	T_EQ("SessionData payload length", unwrapped_length, device_request_length);
	T_OK("SessionData round trip", memcmp(unwrapped, device_request, device_request_length) == 0);

	uint8_t do53[320];
	size_t do53_length = 0;
	T_EQ("wrap DO53", woz_aliro_wrap_do53(session_data, session_data_length,
		do53, sizeof(do53), &do53_length), 0);
	T_EQ("unwrap DO53", woz_aliro_unwrap_do53(do53, do53_length,
		&unwrapped, &unwrapped_length), 0);
	T_EQ("DO53 payload length", unwrapped_length, session_data_length);

	/* Force command chaining and check CLA/Le rules. */
	uint8_t envelope[80];
	size_t envelope_length = 0;
	size_t envelope_offset = 0;
	bool last = true;
	T_EQ("build chained ENVELOPE", woz_aliro_build_envelope_command(
		do53, do53_length, &envelope_offset, 32, 256, false, envelope,
		sizeof(envelope), &envelope_length, &last), 0);
	T_OK("first ENVELOPE is chained", !last && envelope[0] == 0x10 && envelope[1] == 0xc3);
	T_EQ("chained ENVELOPE has no Le", envelope_length, 5 + 32);
	while (!last) {
		T_EQ("build next ENVELOPE", woz_aliro_build_envelope_command(
			do53, do53_length, &envelope_offset, 32, 256, false, envelope,
			sizeof(envelope), &envelope_length, &last), 0);
	}
	T_OK("last ENVELOPE clears chaining", envelope[0] == 0x00 && envelope[1] == 0xc3);
	T_OK("last ENVELOPE carries Le", envelope[envelope_length - 1] == 0x00);

	/* Extended-length support is permission, not a request to use the longer
	 * representation when the command and response both fit a short APDU. */
	static const uint8_t small_do53[] = { 0x53, 0x03, 0x01, 0x02, 0x03 };
	static const uint8_t expected_short_envelope[] = {
		0x00, 0xc3, 0x00, 0x00, 0x05,
		0x53, 0x03, 0x01, 0x02, 0x03, 0xf0,
	};
	envelope_offset = 0;
	T_EQ("build short ENVELOPE with extended support", woz_aliro_build_envelope_command(
		small_do53, sizeof(small_do53), &envelope_offset, 240, 240, true,
		envelope, sizeof(envelope), &envelope_length, &last), 0);
	T_OK("supported extended ENVELOPE stays short",
		envelope_length == sizeof(expected_short_envelope) &&
		memcmp(envelope, expected_short_envelope,
		       sizeof(expected_short_envelope)) == 0);

	uint8_t collected[16];
	size_t collected_length = 0, next_length = 0;
	const uint8_t first_response[] = { 1, 2, 3, 0x61, 0x04 };
	T_EQ("collect 61xx response", woz_aliro_collect_response(first_response,
		sizeof(first_response), collected, sizeof(collected), &collected_length,
		&next_length), WOZ_ALIRO_STEP_UP_MORE_RESPONSE);
	T_EQ("61xx suggested length", next_length, 4);
	uint8_t get_response[8];
	size_t get_response_length = 0;
	T_EQ("build GET RESPONSE", woz_aliro_build_get_response_command(next_length,
		get_response, sizeof(get_response), &get_response_length), 0);
	static const uint8_t expected_get_response[] = { 0, 0xc0, 0, 0, 4 };
	T_OK("GET RESPONSE bytes", get_response_length == sizeof(expected_get_response) &&
		memcmp(get_response, expected_get_response, sizeof(expected_get_response)) == 0);
	const uint8_t final_response[] = { 4, 5, 0x90, 0x00 };
	T_EQ("collect final response", woz_aliro_collect_response(final_response,
		sizeof(final_response), collected, sizeof(collected), &collected_length,
		&next_length), WOZ_ALIRO_STEP_UP_OK);
	T_EQ("collected response length", collected_length, 5);

	/* Published plaintext DeviceResponse: pin compact aliases and the views
	 * consumed by the cryptographic validation layer. */
	uint8_t device_response[768];
	const size_t device_response_length = decode_hex(
		"a3613163312e30613281a26131a26131a167616c69726f2d6181d8185838a4613101613258200aa260c85ca2f6eca90016720a1d7c7c160baf9cfa1a5aa4156331b71863b426613368656c656d656e74326134a1000161328443a10126a104478ea23b8fe54e51590133d81859012ea7613163312e306132675348412d3235366133a167616c69726f2d61a3005820b193e9b1fd40d43aee51f794fb2754f537a12104b743f53ede26d4a74ef604660158202f6f396adb893a91242c60f3b3a32237c90f543cbbed2bf10398ac228955b7e902582095feb0333d71a311b94921230db1bcd094629c01d0fe5e1f2ab6d888b8997ca36134a16134a40102200121582096313d6c63e24e3372742bfdb1a33ba2c897dcd68ab8c753e4fbd48dca6b7f9a2258201fb3269edd418857de1b39a4e4a44b92fa484caa722c228288f01d0c03a2c3d6613567616c69726f2d616136a36131c074323032342d30362d30315431333a33303a30325a6132c074323032342d30362d30315431333a33303a30325a6133c074323032352d30362d30315431333a33303a30325a6137f5584007df311fce5e28c83b5b88e6402fae24250c778eec0c58e06283a7d6ab7037e791307aadb8571b1229e18c49932de464a4dc4f639ad186eb8742099b56a15d17613567616c69726f2d61613300",
		device_response, sizeof(device_response));
	struct woz_aliro_access_document access_document;
	T_EQ("parse published Access Document", woz_aliro_parse_access_document(
		device_response, device_response_length, (const uint8_t *)"element2", 8,
		&access_document), 0);
	T_EQ("Access Document digest ID", access_document.digest_id, 1);
	T_OK("Access Document device key", access_document.device_public_key[0] == 4 &&
		access_document.device_public_key[1] == 0x96 && access_document.device_public_key[64] == 0xd6);
	T_OK("Access Document kid", access_document.issuer_kid_length == 7 &&
		access_document.issuer_kid[0] == 0x8e);
	T_OK("Access Document signed time", memcmp(access_document.signed_timestamp,
		"2024-06-01T13:30:02Z", 20) == 0);
	T_OK("Access Document requires time", access_document.time_verification_required);

	/* Table 7-1 assigns compact key "1" to deviceKey inside deviceKeyInfo.
	 * Keep accepting the older published fixture's "4", but pin the current
	 * encoding observed from a real User Device as well. */
	static const uint8_t legacy_device_key_path[] = { 0x61, 0x34, 0xa1, 0x61, 0x34, 0xa4 };
	bool updated_device_key_alias = false;
	for (size_t i = 0; i + sizeof(legacy_device_key_path) <= device_response_length; ++i) {
		if (memcmp(device_response + i, legacy_device_key_path,
			   sizeof(legacy_device_key_path)) == 0) {
			device_response[i + 4] = 0x31;
			updated_device_key_alias = true;
			break;
		}
	}
	T_OK("Access Document deviceKey alias fixture", updated_device_key_alias);
	T_EQ("parse standard deviceKey alias", woz_aliro_parse_access_document(
		device_response, device_response_length, (const uint8_t *)"element2", 8,
		&access_document), 0);

	/* SELECT corners: the step-up AID and proprietary-information policing. */
	uint8_t step_up_select[WOZ_ALIRO_SELECT_COMMAND_SIZE];
	T_EQ("build step-up SELECT",
	     woz_aliro_build_select_command(WOZ_ALIRO_SELECT_STEP_UP, step_up_select),
	     WOZ_ALIRO_SELECT_OK);
	T_OK("step-up SELECT AID", memcmp(step_up_select + 5, woz_aliro_step_up_aid,
					  WOZ_ALIRO_AID_SIZE) == 0);
	T_EQ("reject unknown SELECT phase",
	     woz_aliro_build_select_command((enum woz_aliro_select_phase)0, step_up_select),
	     WOZ_ALIRO_SELECT_INVALID_ARGUMENT);

	struct woz_aliro_select_response select_result;
	T_EQ("reject null proprietary TLV",
	     woz_aliro_parse_proprietary_information(NULL, 0, WOZ_ALIRO_SELECT_EXPEDITED,
						     &select_result),
	     WOZ_ALIRO_SELECT_INVALID_ARGUMENT);
	static const uint8_t not_a5[] = { 0x80, 0x01, 0xaa };
	T_EQ("reject non-A5 proprietary TLV",
	     woz_aliro_parse_proprietary_information(not_a5, sizeof(not_a5),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t inner_truncated[] = { 0xa5, 0x01, 0x80 };
	T_EQ("reject truncated proprietary item",
	     woz_aliro_parse_proprietary_information(inner_truncated, sizeof(inner_truncated),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t wrong_type[] = { 0xa5, 0x04, 0x80, 0x02, 0x00, 0x01 };
	T_EQ("reject wrong application type",
	     woz_aliro_parse_proprietary_information(wrong_type, sizeof(wrong_type),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_WRONG_TYPE);
	static const uint8_t duplicate_type[] = { 0xa5, 0x08, 0x80, 0x02, 0x00, 0x00,
						  0x80, 0x02, 0x00, 0x00 };
	T_EQ("reject duplicate application type",
	     woz_aliro_parse_proprietary_information(duplicate_type, sizeof(duplicate_type),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_WRONG_TYPE);
	static const uint8_t missing_type[] = { 0xa5, 0x04, 0x5c, 0x02, 0x01, 0x00 };
	T_EQ("reject missing application type",
	     woz_aliro_parse_proprietary_information(missing_type, sizeof(missing_type),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_WRONG_TYPE);
	static const uint8_t odd_versions[] = { 0xa5, 0x07, 0x80, 0x02, 0x00, 0x00,
						0x5c, 0x01, 0x01 };
	T_EQ("reject odd version list",
	     woz_aliro_parse_proprietary_information(odd_versions, sizeof(odd_versions),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t small_limits[] = { 0xa5, 0x13, 0x80, 0x02, 0x00, 0x00,
						0x5c, 0x02, 0x01, 0x00, 0x7f, 0x66, 0x08,
						0x02, 0x02, 0x00, 0xff, 0x02, 0x02, 0x0e, 0x00 };
	T_EQ("reject sub-minimum extended limits",
	     woz_aliro_parse_proprietary_information(small_limits, sizeof(small_limits),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t wrong_size_tag[] = { 0xa5, 0x0f, 0x80, 0x02, 0x00, 0x00,
						  0x5c, 0x02, 0x01, 0x00, 0x7f, 0x66, 0x04,
						  0x03, 0x02, 0x0e, 0x00 };
	T_EQ("reject non-integer extended limit",
	     woz_aliro_parse_proprietary_information(wrong_size_tag, sizeof(wrong_size_tag),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t trailing_size[] = { 0xa5, 0x14, 0x80, 0x02, 0x00, 0x00,
						 0x5c, 0x02, 0x01, 0x00, 0x7f, 0x66, 0x09,
						 0x02, 0x02, 0x0e, 0x00, 0x02, 0x02, 0x0e,
						 0x00, 0x00 };
	T_EQ("reject trailing extended-limit data",
	     woz_aliro_parse_proprietary_information(trailing_size, sizeof(trailing_size),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t duplicate_extended[] = { 0xa5, 0x1e, 0x80, 0x02, 0x00, 0x00,
						      0x5c, 0x02, 0x01, 0x00, 0x7f, 0x66, 0x08,
						      0x02, 0x02, 0x0e, 0x00, 0x02, 0x02, 0x0e,
						      0x00, 0x7f, 0x66, 0x08, 0x02, 0x02, 0x0e,
						      0x00, 0x02, 0x02, 0x0e, 0x00 };
	T_EQ("reject duplicate extended limits",
	     woz_aliro_parse_proprietary_information(duplicate_extended,
						     sizeof(duplicate_extended),
						     WOZ_ALIRO_SELECT_EXPEDITED, &select_result),
	     WOZ_ALIRO_SELECT_INVALID_APDU);

	uint16_t step_up_version = 0xffff;
	static const uint8_t step_up_response[] = {
		0x6f, 0x11, 0x84, 0x09, 0xa0, 0x00, 0x00, 0x09, 0x09, 0xac,
		0xce, 0x55, 0x02, 0xa5, 0x04, 0x80, 0x02, 0x00, 0x00, 0x90, 0x00,
	};
	T_EQ("parse step-up SELECT response",
	     woz_aliro_parse_select_response(step_up_response, sizeof(step_up_response),
					     WOZ_ALIRO_SELECT_STEP_UP, &step_up_version),
	     WOZ_ALIRO_SELECT_OK);
	T_EQ("reject null version out",
	     woz_aliro_parse_select_response(step_up_response, sizeof(step_up_response),
					     WOZ_ALIRO_SELECT_STEP_UP, NULL),
	     WOZ_ALIRO_SELECT_INVALID_ARGUMENT);
	T_EQ("reject short SELECT response",
	     woz_aliro_parse_select_response(step_up_response, 3, WOZ_ALIRO_SELECT_STEP_UP,
					     &step_up_version), WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t not_fci[] = { 0x70, 0x02, 0x00, 0x00, 0x90, 0x00 };
	T_EQ("reject non-FCI response",
	     woz_aliro_parse_select_response(not_fci, sizeof(not_fci),
					     WOZ_ALIRO_SELECT_STEP_UP, &step_up_version),
	     WOZ_ALIRO_SELECT_INVALID_APDU);
	static const uint8_t missing_proprietary[] = {
		0x6f, 0x0b, 0x84, 0x09, 0xa0, 0x00, 0x00, 0x09, 0x09, 0xac,
		0xce, 0x55, 0x02, 0x90, 0x00,
	};
	T_EQ("reject FCI without proprietary",
	     woz_aliro_parse_select_response(missing_proprietary, sizeof(missing_proprietary),
					     WOZ_ALIRO_SELECT_STEP_UP, &step_up_version),
	     WOZ_ALIRO_SELECT_INVALID_APDU);

	/* AUTH argument, status, and optional-field corners. */
	uint8_t auth_scratch[192];
	size_t auth_scratch_length = 0;
	T_EQ("AUTH0 rejects null params",
	     woz_aliro_build_auth0_command(NULL, auth_scratch, sizeof(auth_scratch),
					   &auth_scratch_length), WOZ_ALIRO_AUTH_INVALID_ARGUMENT);
	struct woz_aliro_auth0_command bad_auth0 = auth0;
	bad_auth0.authentication_policy = 4;
	T_EQ("AUTH0 rejects bad policy",
	     woz_aliro_build_auth0_command(&bad_auth0, auth_scratch, sizeof(auth_scratch),
					   &auth_scratch_length), WOZ_ALIRO_AUTH_INVALID_ARGUMENT);
	T_EQ("AUTH0 rejects short buffer",
	     woz_aliro_build_auth0_command(&auth0, auth_scratch, 16, &auth_scratch_length),
	     WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL);
	static const uint8_t vendor_extension[] = { 0x01, 0x02, 0x03, 0x04 };
	struct woz_aliro_auth0_command vendor_auth0 = auth0;
	vendor_auth0.vendor_extension = vendor_extension;
	vendor_auth0.vendor_extension_length = sizeof(vendor_extension);
	T_EQ("AUTH0 with vendor extension",
	     woz_aliro_build_auth0_command(&vendor_auth0, auth_scratch, sizeof(auth_scratch),
					   &auth_scratch_length), WOZ_ALIRO_AUTH_OK);
	T_EQ("AUTH0 vendor extension length", auth_scratch_length,
	     WOZ_ALIRO_AUTH0_STANDARD_COMMAND_SIZE + 6);
	vendor_auth0.vendor_extension_length = 130;
	T_EQ("AUTH0 rejects oversize vendor extension",
	     woz_aliro_build_auth0_command(&vendor_auth0, auth_scratch, sizeof(auth_scratch),
					   &auth_scratch_length), WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL);

	T_EQ("AUTH0 response rejects null",
	     woz_aliro_parse_auth0_response(NULL, 0, 0, &auth0_response),
	     WOZ_ALIRO_AUTH_INVALID_ARGUMENT);
	static const uint8_t status_only[] = { 0x90 };
	T_EQ("AUTH0 response rejects short APDU",
	     woz_aliro_parse_auth0_response(status_only, sizeof(status_only), 0,
					     &auth0_response), WOZ_ALIRO_AUTH_INVALID_APDU);
	static const uint8_t auth_status_error[] = { 0x69, 0x85 };
	T_EQ("AUTH0 response rejects bad status",
	     woz_aliro_parse_auth0_response(auth_status_error, sizeof(auth_status_error), 0,
					     &auth0_response), WOZ_ALIRO_AUTH_STATUS_ERROR);
	static const uint8_t wrong_first_tag[] = { 0x41, 0x01, 0x00, 0x90, 0x00 };
	T_EQ("AUTH0 response rejects wrong first tag",
	     woz_aliro_parse_auth0_response(wrong_first_tag, sizeof(wrong_first_tag), 0,
					     &auth0_response), WOZ_ALIRO_AUTH_WRONG_CONTENT);

	uint8_t point[WOZ_ALIRO_PUBLIC_KEY_SIZE] = { 0x04 };
	uint8_t cryptogram[64] = { 0 };
	uint8_t fast_vendor[192];
	size_t fast_vendor_length = 0;
	T_EQ("assemble fast key TLV", woz_aliro_tlv_write(fast_vendor, sizeof(fast_vendor),
	     &fast_vendor_length, 0x86, point, sizeof(point)), WOZ_ALIRO_TLV_OK);
	T_EQ("assemble fast cryptogram TLV", woz_aliro_tlv_write(fast_vendor,
	     sizeof(fast_vendor), &fast_vendor_length, 0x9d, cryptogram, sizeof(cryptogram)),
	     WOZ_ALIRO_TLV_OK);
	T_EQ("assemble fast vendor TLV", woz_aliro_tlv_write(fast_vendor, sizeof(fast_vendor),
	     &fast_vendor_length, 0xb2, vendor_extension, sizeof(vendor_extension)),
	     WOZ_ALIRO_TLV_OK);
	fast_vendor[fast_vendor_length++] = 0x90;
	fast_vendor[fast_vendor_length++] = 0x00;
	T_EQ("AUTH0 fast response with vendor extension",
	     woz_aliro_parse_auth0_response(fast_vendor, fast_vendor_length, 1,
					     &auth0_response), WOZ_ALIRO_AUTH_OK);
	T_EQ("AUTH0 vendor extension parsed", auth0_response.vendor_extension_length,
	     sizeof(vendor_extension));

	fast_vendor_length = 0;
	T_EQ("assemble dangling key TLV", woz_aliro_tlv_write(fast_vendor,
	     sizeof(fast_vendor), &fast_vendor_length, 0x86, point, sizeof(point)),
	     WOZ_ALIRO_TLV_OK);
	fast_vendor[fast_vendor_length++] = 0x9d;
	fast_vendor[fast_vendor_length++] = 0x90;
	fast_vendor[fast_vendor_length++] = 0x00;
	T_EQ("AUTH0 response rejects dangling tag",
	     woz_aliro_parse_auth0_response(fast_vendor, fast_vendor_length, 1,
					     &auth0_response), WOZ_ALIRO_AUTH_INVALID_APDU);

	fast_vendor_length = 0;
	T_EQ("assemble key TLV again", woz_aliro_tlv_write(fast_vendor, sizeof(fast_vendor),
	     &fast_vendor_length, 0x86, point, sizeof(point)), WOZ_ALIRO_TLV_OK);
	static const uint8_t stray_byte = 0x00;
	T_EQ("assemble stray TLV", woz_aliro_tlv_write(fast_vendor, sizeof(fast_vendor),
	     &fast_vendor_length, 0x41, &stray_byte, 1), WOZ_ALIRO_TLV_OK);
	fast_vendor[fast_vendor_length++] = 0x90;
	fast_vendor[fast_vendor_length++] = 0x00;
	T_EQ("AUTH0 response rejects stray tag",
	     woz_aliro_parse_auth0_response(fast_vendor, fast_vendor_length, 0,
					     &auth0_response), WOZ_ALIRO_AUTH_WRONG_CONTENT);

	T_EQ("auth data rejects null reader",
	     woz_aliro_build_authentication_data(NULL, point, point, transaction_id, 0,
						 authentication_data),
	     WOZ_ALIRO_AUTH_INVALID_ARGUMENT);
	uint8_t compressed_point[WOZ_ALIRO_PUBLIC_KEY_SIZE] = { 0x02 };
	T_EQ("auth data rejects compressed key",
	     woz_aliro_build_authentication_data(reader_id, compressed_point, point,
						 transaction_id, 0, authentication_data),
	     WOZ_ALIRO_AUTH_INVALID_ARGUMENT);

	T_EQ("AUTH1 rejects null signature",
	     woz_aliro_build_auth1_command(0, NULL, auth_scratch, sizeof(auth_scratch),
					   &auth_scratch_length), WOZ_ALIRO_AUTH_INVALID_ARGUMENT);
	uint8_t certificate[100];
	memset(certificate, 0x33, sizeof(certificate));
	T_EQ("AUTH1 with certificate",
	     woz_aliro_build_auth1_command_ex(0, signature, certificate, sizeof(certificate),
					      auth_scratch, sizeof(auth_scratch),
					      &auth_scratch_length), WOZ_ALIRO_AUTH_OK);
	T_EQ("AUTH1 certificate length", auth_scratch_length,
	     WOZ_ALIRO_AUTH1_COMMAND_SIZE + 102);
	T_EQ("AUTH1 certificate rejects short buffer",
	     woz_aliro_build_auth1_command_ex(0, signature, certificate, sizeof(certificate),
					      auth_scratch, 80, &auth_scratch_length),
	     WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL);
}
