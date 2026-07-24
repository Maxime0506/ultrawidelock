/* Aliro 1.0 expedited authentication APDU codecs. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WOZ_ALIRO_PUBLIC_KEY_SIZE             65
#define WOZ_ALIRO_SIGNATURE_SIZE              64
#define WOZ_ALIRO_TRANSACTION_ID_SIZE         16
#define WOZ_ALIRO_READER_ID_SIZE              32
#define WOZ_ALIRO_AUTH0_STANDARD_COMMAND_SIZE 135
#define WOZ_ALIRO_AUTH1_COMMAND_SIZE          75
#define WOZ_ALIRO_AUTH_DATA_SIZE              126

enum woz_aliro_auth_result {
	WOZ_ALIRO_AUTH_OK = 0,
	WOZ_ALIRO_AUTH_INVALID_ARGUMENT = -1,
	WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL = -2,
	WOZ_ALIRO_AUTH_INVALID_APDU = -3,
	WOZ_ALIRO_AUTH_STATUS_ERROR = -4,
	WOZ_ALIRO_AUTH_WRONG_CONTENT = -5,
};

struct woz_aliro_auth0_command {
	uint8_t command_parameters;
	uint8_t authentication_policy;
	uint16_t protocol_version;
	const uint8_t *reader_ephemeral_public_key;
	const uint8_t *transaction_identifier;
	const uint8_t *reader_identifier;
	const uint8_t *vendor_extension;
	size_t vendor_extension_length;
};

struct woz_aliro_auth0_response {
	uint8_t credential_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE];
	const uint8_t *cryptogram;
	size_t cryptogram_length;
	const uint8_t *vendor_extension;
	size_t vendor_extension_length;
};

struct woz_aliro_auth1_response {
	uint8_t credential_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE];
	uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE];
	uint16_t signaling_bitmap;
	const uint8_t *credential_signed_timestamp;
	const uint8_t *revocation_signed_timestamp;
};

int woz_aliro_build_auth0_command(const struct woz_aliro_auth0_command *params, uint8_t *output,
				  size_t output_capacity, size_t *output_length);

int woz_aliro_parse_auth0_response(const uint8_t *response, size_t response_length,
				   int fast_requested, struct woz_aliro_auth0_response *result);

int woz_aliro_build_authentication_data(
	const uint8_t reader_identifier[WOZ_ALIRO_READER_ID_SIZE],
	const uint8_t credential_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE],
	const uint8_t reader_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE],
	const uint8_t transaction_identifier[WOZ_ALIRO_TRANSACTION_ID_SIZE], uint32_t usage,
	uint8_t output[WOZ_ALIRO_AUTH_DATA_SIZE]);

int woz_aliro_build_auth1_command(uint8_t command_parameters,
				  const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE],
				  uint8_t *output, size_t output_capacity, size_t *output_length);

int woz_aliro_build_auth1_command_ex(uint8_t command_parameters,
				     const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE],
				     const uint8_t *reader_certificate,
				     size_t reader_certificate_length, uint8_t *output,
				     size_t output_capacity, size_t *output_length);

int woz_aliro_parse_auth1_plaintext(const uint8_t *plaintext, size_t plaintext_length,
				    int public_key_requested,
				    struct woz_aliro_auth1_response *result);

#ifdef __cplusplus
}
#endif
