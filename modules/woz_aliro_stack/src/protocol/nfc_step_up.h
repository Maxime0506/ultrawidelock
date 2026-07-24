/* Aliro 1.0 / ISO 18013-5 NFC step-up message and APDU codecs. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum woz_aliro_step_up_result {
	WOZ_ALIRO_STEP_UP_OK = 0,
	WOZ_ALIRO_STEP_UP_MORE_RESPONSE = 1,
	WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT = -1,
	WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL = -2,
	WOZ_ALIRO_STEP_UP_INVALID_DATA = -3,
	WOZ_ALIRO_STEP_UP_STATUS_ERROR = -4,
};

/* Build the compact-key Aliro DeviceRequest. */
int woz_aliro_build_device_request(const uint8_t *element_identifier,
				   size_t element_identifier_length, bool intent_to_store,
				   uint8_t *output, size_t output_capacity, size_t *output_length);

/* Encode/decode ISO 18013-5 SessionData: { "data": bstr }. */
int woz_aliro_wrap_session_data(const uint8_t *ciphertext, size_t ciphertext_length,
				uint8_t *output, size_t output_capacity, size_t *output_length);
int woz_aliro_unwrap_session_data(const uint8_t *session_data, size_t session_data_length,
				  const uint8_t **ciphertext, size_t *ciphertext_length);

/* Encode/decode the NFC Device Engagement DO53 wrapper. */
int woz_aliro_wrap_do53(const uint8_t *message, size_t message_length, uint8_t *output,
			size_t output_capacity, size_t *output_length);
int woz_aliro_unwrap_do53(const uint8_t *encoded, size_t encoded_length, const uint8_t **message,
			  size_t *message_length);

/* Build one ENVELOPE command. offset is advanced by the emitted fragment. */
int woz_aliro_build_envelope_command(const uint8_t *encoded_do53, size_t encoded_length,
				     size_t *offset, size_t max_command_data,
				     size_t max_response_data, bool extended_supported,
				     uint8_t *output, size_t output_capacity, size_t *output_length,
				     bool *last_fragment);

int woz_aliro_build_get_response_command(size_t expected_length, uint8_t *output,
					 size_t output_capacity, size_t *output_length);

/* Append response data and interpret 9000 / 61xx. sw2==0 means 256 bytes. */
int woz_aliro_collect_response(const uint8_t *response, size_t response_length, uint8_t *collected,
			       size_t collected_capacity, size_t *collected_length,
			       size_t *next_length);

#ifdef __cplusplus
}
#endif
