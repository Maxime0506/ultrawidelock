/* SPDX-License-Identifier: ISC */

/**
 * @file nfc_select.c
 * NFC SELECT command builder and response parser for the credential protocol. build_select_command
 * emits 00 A4 04 00 09 `AID` 00. parse_proprietary_information decodes type-0x80 data from a SELECT
 * response, extracting protocol version (expedited phase only) and extended-length sizes (0x7f66
 * TLV). parse_select_response and parse_select_response_ex validate the trailing 9000, check AID,
 * and call parse_proprietary_information.
 */
#include "nfc_select.h"

#include <ultrawidelock/tlv.h>

#include <stdbool.h>
#include <string.h>

const uint8_t ultrawidelock_cred_expedited_aid[ULTRAWIDELOCK_CRED_AID_SIZE] = {
	0xa0, 0x00, 0x00, 0x09, 0x09, 0xac, 0xce, 0x55, 0x01,
};

const uint8_t ultrawidelock_cred_step_up_aid[ULTRAWIDELOCK_CRED_AID_SIZE] = {
	0xa0, 0x00, 0x00, 0x09, 0x09, 0xac, 0xce, 0x55, 0x02,
};

/**
 * Map a SELECT phase (EXPEDITED or STEP_UP) to its corresponding credential AID byte sequence;
 * return NULL if phase is unknown.
 */
static const uint8_t *aid_for_phase(enum ultrawidelock_cred_select_phase phase)
{
	if (phase == ULTRAWIDELOCK_CRED_SELECT_EXPEDITED) {
		return ultrawidelock_cred_expedited_aid;
	}
	if (phase == ULTRAWIDELOCK_CRED_SELECT_STEP_UP) {
		return ultrawidelock_cred_step_up_aid;
	}
	return NULL;
}

int ultrawidelock_cred_build_select_command(enum ultrawidelock_cred_select_phase phase,
				   uint8_t out[ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE])
{
	const uint8_t *aid = aid_for_phase(phase);
	if (aid == NULL || out == NULL) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_ARGUMENT;
	}
	const uint8_t header[] = {0x00, 0xa4, 0x04, 0x00, ULTRAWIDELOCK_CRED_AID_SIZE};
	memcpy(out, header, sizeof(header));
	memcpy(out + sizeof(header), aid, ULTRAWIDELOCK_CRED_AID_SIZE);
	out[ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE - 1] = 0x00;
	return ULTRAWIDELOCK_CRED_SELECT_OK;
}

/**
 * Parse proprietary information TLV data from a SELECT response: validate type tag 0x80, extract
 * protocol version from expedited phase 0x5c, and decode extended-length sizes from 0x7f66 if
 * present; return ULTRAWIDELOCK_CRED_SELECT_OK on valid parse or error code if format or content is
 * malformed.
 */
static int parse_proprietary_information(const uint8_t *data, size_t length,
					 enum ultrawidelock_cred_select_phase phase,
					 struct ultrawidelock_cred_select_response *result)
{
	size_t offset = 0;
	bool found_type = false;
	bool found_version = false;
	while (offset < length) {
		struct ultrawidelock_cred_tlv tlv;
		if (ultrawidelock_cred_tlv_next(data, length, &offset, &tlv) != ULTRAWIDELOCK_CRED_TLV_OK) {
			return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
		}
		if (tlv.tag == 0x80) {
			if (found_type || tlv.length != 2 || tlv.value[0] != 0 ||
			    tlv.value[1] != 0) {
				return ULTRAWIDELOCK_CRED_SELECT_WRONG_TYPE;
			}
			found_type = true;
		} else if (tlv.tag == 0x5c && phase == ULTRAWIDELOCK_CRED_SELECT_EXPEDITED) {
			if (found_version || tlv.length == 0 || (tlv.length % 2) != 0) {
				return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
			}
			found_version = true;
			for (size_t i = 0; i < tlv.length; i += 2) {
				const uint16_t version =
					((uint16_t)tlv.value[i] << 8) | tlv.value[i + 1];
				if (version == 0x0100) {
					result->selected_protocol_version = version;
				}
			}
		} else if (tlv.tag == 0x7f66) {
			if (result->extended_length_supported) {
				return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
			}
			size_t inner = 0;
			struct ultrawidelock_cred_tlv sizes[2];
			for (size_t i = 0; i < 2; ++i) {
				if (ultrawidelock_cred_tlv_next(tlv.value, tlv.length, &inner, &sizes[i]) !=
					    ULTRAWIDELOCK_CRED_TLV_OK ||
				    sizes[i].tag != 0x02 || sizes[i].length != 2) {
					return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
				}
			}
			if (inner != tlv.length) {
				return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
			}
			result->max_command_data_length =
				((size_t)sizes[0].value[0] << 8) | sizes[0].value[1];
			result->max_response_data_length =
				((size_t)sizes[1].value[0] << 8) | sizes[1].value[1];
			if (result->max_command_data_length < 256 ||
			    result->max_response_data_length < 257) {
				return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
			}
			result->extended_length_supported = 1;
		}
	}
	if (!found_type) {
		return ULTRAWIDELOCK_CRED_SELECT_WRONG_TYPE;
	}
	if (phase == ULTRAWIDELOCK_CRED_SELECT_EXPEDITED &&
	    (!found_version || result->selected_protocol_version == 0)) {
		return ULTRAWIDELOCK_CRED_SELECT_VERSION_NOT_SUPPORTED;
	}
	return ULTRAWIDELOCK_CRED_SELECT_OK;
}

int ultrawidelock_cred_parse_proprietary_information(const uint8_t *encoded, size_t encoded_length,
					    enum ultrawidelock_cred_select_phase phase,
					    struct ultrawidelock_cred_select_response *result)
{
	if (encoded == NULL || result == NULL || aid_for_phase(phase) == NULL) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_ARGUMENT;
	}
	memset(result, 0, sizeof(*result));
	result->max_command_data_length = 255;
	result->max_response_data_length = 256;
	struct ultrawidelock_cred_tlv proprietary;
	size_t offset = 0;
	if (ultrawidelock_cred_tlv_next(encoded, encoded_length, &offset, &proprietary) !=
		    ULTRAWIDELOCK_CRED_TLV_OK ||
	    proprietary.tag != 0xa5 || offset != encoded_length) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
	}
	result->proprietary_information_tlv = encoded;
	result->proprietary_information_tlv_length = encoded_length;
	return parse_proprietary_information(proprietary.value, proprietary.length, phase, result);
}

int ultrawidelock_cred_parse_select_response(const uint8_t *response, size_t response_length,
				    enum ultrawidelock_cred_select_phase phase,
				    uint16_t *selected_protocol_version)
{
	if (selected_protocol_version == NULL) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_ARGUMENT;
	}
	struct ultrawidelock_cred_select_response result = {0};
	const int status =
		ultrawidelock_cred_parse_select_response_ex(response, response_length, phase, &result);
	*selected_protocol_version = result.selected_protocol_version;
	return status;
}

/**
 * Parse a complete ISO 7816 SELECT response APDU (ending with 9000), validate the AID matches the
 * requested phase (EXPEDITED or STEP_UP), extract proprietary information TLV, and decode protocol
 * version and extended-length sizes. Return ULTRAWIDELOCK_CRED_SELECT_OK on success or an error
 * code (WRONG_APPLICATION, STATUS_ERROR, INVALID_APDU, INVALID_ARGUMENT).
 */
int ultrawidelock_cred_parse_select_response_ex(const uint8_t *response, size_t response_length,
				       enum ultrawidelock_cred_select_phase phase,
				       struct ultrawidelock_cred_select_response *result)
{
	const uint8_t *expected_aid = aid_for_phase(phase);
	if (response == NULL || result == NULL || expected_aid == NULL) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_ARGUMENT;
	}
	memset(result, 0, sizeof(*result));
	result->max_command_data_length = 255;
	result->max_response_data_length = 256;
	if (response_length < 4) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
	}
	if (response[response_length - 2] != 0x90 || response[response_length - 1] != 0x00) {
		return ULTRAWIDELOCK_CRED_SELECT_STATUS_ERROR;
	}

	const size_t data_length = response_length - 2;
	size_t outer_offset = 0;
	struct ultrawidelock_cred_tlv fci;
	if (ultrawidelock_cred_tlv_next(response, data_length, &outer_offset, &fci) !=
		    ULTRAWIDELOCK_CRED_TLV_OK ||
	    fci.tag != 0x6f || outer_offset != data_length) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
	}

	size_t offset = 0;
	bool found_aid = false;
	bool found_proprietary = false;
	while (offset < fci.length) {
		const size_t tlv_start = offset;
		struct ultrawidelock_cred_tlv tlv;
		if (ultrawidelock_cred_tlv_next(fci.value, fci.length, &offset, &tlv) !=
		    ULTRAWIDELOCK_CRED_TLV_OK) {
			return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
		}
		if (tlv.tag == 0x84) {
			if (found_aid || tlv.length != ULTRAWIDELOCK_CRED_AID_SIZE ||
			    memcmp(tlv.value, expected_aid, ULTRAWIDELOCK_CRED_AID_SIZE) != 0) {
				return ULTRAWIDELOCK_CRED_SELECT_WRONG_APPLICATION;
			}
			found_aid = true;
		} else if (tlv.tag == 0xa5) {
			if (found_proprietary) {
				return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
			}
			found_proprietary = true;
			struct ultrawidelock_cred_select_response proprietary_result;
			const int status = ultrawidelock_cred_parse_proprietary_information(
				fci.value + tlv_start, tlv.encoded_length, phase,
				&proprietary_result);
			if (status != ULTRAWIDELOCK_CRED_SELECT_OK) {
				return status;
			}
			*result = proprietary_result;
		}
	}
	if (!found_aid || !found_proprietary) {
		return ULTRAWIDELOCK_CRED_SELECT_INVALID_APDU;
	}
	return ULTRAWIDELOCK_CRED_SELECT_OK;
}
