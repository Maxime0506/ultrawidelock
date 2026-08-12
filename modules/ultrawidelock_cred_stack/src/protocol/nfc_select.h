/**
 * @file nfc_select.h
 * Parsed result of an NFC SELECT command for the credential applet: negotiated protocol version,
 * maximum command and response data lengths (from TLV or default), extended-length support, and the
 * raw proprietary information TLV (A5 tag) for further parsing.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	ULTRAWIDELOCK_CRED_AID_SIZE = 9,
	ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE = 15,
};

enum ultrawidelock_cred_select_phase {
	ULTRAWIDELOCK_CRED_SELECT_EXPEDITED = 1,
	ULTRAWIDELOCK_CRED_SELECT_STEP_UP = 2,
};

enum ultrawidelock_cred_select_result {
	ULTRAWIDELOCK_CRED_SELECT_OK = 0,
	ULTRAWIDELOCK_CRED_SELECT_INVALID_ARGUMENT = -1,
	ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU = -2,
	ULTRAWIDELOCK_CRED_SELECT_STATUS_ERROR = -3,
	ULTRAWIDELOCK_CRED_SELECT_WRONG_APPLICATION = -4,
	ULTRAWIDELOCK_CRED_SELECT_WRONG_TYPE = -5,
	ULTRAWIDELOCK_CRED_SELECT_VERSION_NOT_SUPPORTED = -6,
};

/**
 * Parsed NFC SELECT response: negotiated protocol version, max command/response data lengths (from
 * 7F66 TLV or defaults), extended-length support flag, and the complete proprietary information TLV
 * (A5 tag). The TLV view is valid only as long as the input response buffer is valid.
 */
struct ultrawidelock_cred_select_response {
	uint16_t selected_protocol_version;
	/* Defaults required for a short-only peer when 7F66 is absent. */
	size_t max_command_data_length;
	size_t max_response_data_length;
	int extended_length_supported;
	/* Complete encoded A5 TLV. The view remains valid as long as the input
	 * response buffer remains valid. */
	const uint8_t *proprietary_information_tlv;
	size_t proprietary_information_tlv_length;
};

extern const uint8_t ultrawidelock_cred_expedited_aid[ULTRAWIDELOCK_CRED_AID_SIZE];
extern const uint8_t ultrawidelock_cred_step_up_aid[ULTRAWIDELOCK_CRED_AID_SIZE];

/* Build 00 A4 04 00 09 <AID> 00 (short case-4 SELECT by DF name). */
int ultrawidelock_cred_build_select_command(enum ultrawidelock_cred_select_phase phase,
				   uint8_t out[ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE]);

/* Parse the complete response APDU, including the trailing SW1/SW2. */
int ultrawidelock_cred_parse_select_response(const uint8_t *response, size_t response_length,
				    enum ultrawidelock_cred_select_phase phase,
				    uint16_t *selected_protocol_version);

int ultrawidelock_cred_parse_select_response_ex(const uint8_t *response, size_t response_length,
				       enum ultrawidelock_cred_select_phase phase,
				       struct ultrawidelock_cred_select_response *result);

/* Parse a complete encoded A5 Proprietary Information TLV. This is also the
 * value carried by BLE Initiate Access Protocol, without an NFC FCI wrapper. */
int ultrawidelock_cred_parse_proprietary_information(const uint8_t *encoded, size_t encoded_length,
					    enum ultrawidelock_cred_select_phase phase,
					    struct ultrawidelock_cred_select_response *result);

#ifdef __cplusplus
}
#endif
