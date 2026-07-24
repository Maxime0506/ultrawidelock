#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	WOZ_ALIRO_AID_SIZE = 9,
	WOZ_ALIRO_SELECT_COMMAND_SIZE = 15,
};

enum woz_aliro_select_phase {
	WOZ_ALIRO_SELECT_EXPEDITED = 1,
	WOZ_ALIRO_SELECT_STEP_UP = 2,
};

enum woz_aliro_select_result {
	WOZ_ALIRO_SELECT_OK = 0,
	WOZ_ALIRO_SELECT_INVALID_ARGUMENT = -1,
	WOZ_ALIRO_SELECT_INVALID_APDU = -2,
	WOZ_ALIRO_SELECT_STATUS_ERROR = -3,
	WOZ_ALIRO_SELECT_WRONG_APPLICATION = -4,
	WOZ_ALIRO_SELECT_WRONG_TYPE = -5,
	WOZ_ALIRO_SELECT_VERSION_NOT_SUPPORTED = -6,
};

struct woz_aliro_select_response {
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

extern const uint8_t woz_aliro_expedited_aid[WOZ_ALIRO_AID_SIZE];
extern const uint8_t woz_aliro_step_up_aid[WOZ_ALIRO_AID_SIZE];

/* Build 00 A4 04 00 09 <AID> 00 (short case-4 SELECT by DF name). */
int woz_aliro_build_select_command(enum woz_aliro_select_phase phase,
				   uint8_t out[WOZ_ALIRO_SELECT_COMMAND_SIZE]);

/* Parse the complete response APDU, including the trailing SW1/SW2. */
int woz_aliro_parse_select_response(const uint8_t *response, size_t response_length,
				    enum woz_aliro_select_phase phase,
				    uint16_t *selected_protocol_version);

int woz_aliro_parse_select_response_ex(const uint8_t *response, size_t response_length,
				       enum woz_aliro_select_phase phase,
				       struct woz_aliro_select_response *result);

/* Parse a complete encoded A5 Proprietary Information TLV. This is also the
 * value carried by BLE Initiate Access Protocol, without an NFC FCI wrapper. */
int woz_aliro_parse_proprietary_information(const uint8_t *encoded, size_t encoded_length,
					    enum woz_aliro_select_phase phase,
					    struct woz_aliro_select_response *result);

#ifdef __cplusplus
}
#endif
