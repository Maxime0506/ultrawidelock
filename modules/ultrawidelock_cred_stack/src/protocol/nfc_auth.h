/* SPDX-License-Identifier: ISC */

/* credential 1.0 expedited authentication APDU codecs. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ULTRAWIDELOCK_CRED_PUBLIC_KEY_SIZE             65
#define ULTRAWIDELOCK_CRED_SIGNATURE_SIZE              64
#define ULTRAWIDELOCK_CRED_TRANSACTION_ID_SIZE         16
#define ULTRAWIDELOCK_CRED_READER_ID_SIZE              32
#define ULTRAWIDELOCK_CRED_AUTH0_STANDARD_COMMAND_SIZE 135
#define ULTRAWIDELOCK_CRED_AUTH1_COMMAND_SIZE          75
#define ULTRAWIDELOCK_CRED_AUTH_DATA_SIZE              126

enum ultrawidelock_cred_auth_result {
	ULTRAWIDELOCK_CRED_AUTH_OK = 0,
	ULTRAWIDELOCK_CRED_AUTH_INVALID_ARGUMENT = -1,
	ULTRAWIDELOCK_CRED_AUTH_BUFFER_TOO_SMALL = -2,
	ULTRAWIDELOCK_CRED_AUTH_INVALID_APDU = -3,
	ULTRAWIDELOCK_CRED_AUTH_STATUS_ERROR = -4,
	ULTRAWIDELOCK_CRED_AUTH_WRONG_CONTENT = -5,
};

/**
 * Parsed NFC AUTH0 command: authentication policy and protocol version from the reader, reader
 * ephemeral public key, transaction ID, reader ID, and optional vendor extension.
 */
struct ultrawidelock_cred_auth0_command {
	uint8_t command_parameters;
	uint8_t authentication_policy;
	uint16_t protocol_version;
	const uint8_t *reader_ephemeral_public_key;
	const uint8_t *transaction_identifier;
	const uint8_t *reader_identifier;
	const uint8_t *vendor_extension;
	size_t vendor_extension_length;
};

/**
 * Parsed NFC AUTH0 response: credential ephemeral public key (65 bytes), cryptogram (variable
 * length), and optional vendor extension.
 */
struct ultrawidelock_cred_auth0_response {
	uint8_t credential_ephemeral_public_key[ULTRAWIDELOCK_CRED_PUBLIC_KEY_SIZE];
	const uint8_t *cryptogram;
	size_t cryptogram_length;
	const uint8_t *vendor_extension;
	size_t vendor_extension_length;
};

/**
 * Parsed NFC AUTH1 response: credential public key (65 bytes), signature (variable length),
 * signaling bitmap, and two signed timestamps (credential and revocation, each 20 bytes if
 * present).
 */
struct ultrawidelock_cred_auth1_response {
	uint8_t credential_public_key[ULTRAWIDELOCK_CRED_PUBLIC_KEY_SIZE];
	uint8_t signature[ULTRAWIDELOCK_CRED_SIGNATURE_SIZE];
	uint16_t signaling_bitmap;
	const uint8_t *credential_signed_timestamp;
	const uint8_t *revocation_signed_timestamp;
};

int ultrawidelock_cred_build_auth0_command(const struct ultrawidelock_cred_auth0_command *params,
					   uint8_t *output, size_t output_capacity,
					   size_t *output_length);

int ultrawidelock_cred_parse_auth0_response(const uint8_t *response, size_t response_length,
				   int fast_requested, struct ultrawidelock_cred_auth0_response *result);

int ultrawidelock_cred_build_authentication_data(
	const uint8_t reader_identifier[ULTRAWIDELOCK_CRED_READER_ID_SIZE],
	const uint8_t credential_ephemeral_public_key[ULTRAWIDELOCK_CRED_PUBLIC_KEY_SIZE],
	const uint8_t reader_ephemeral_public_key[ULTRAWIDELOCK_CRED_PUBLIC_KEY_SIZE],
	const uint8_t transaction_identifier[ULTRAWIDELOCK_CRED_TRANSACTION_ID_SIZE], uint32_t usage,
	uint8_t output[ULTRAWIDELOCK_CRED_AUTH_DATA_SIZE]);

int ultrawidelock_cred_build_auth1_command(uint8_t command_parameters,
				  const uint8_t signature[ULTRAWIDELOCK_CRED_SIGNATURE_SIZE],
				  uint8_t *output, size_t output_capacity, size_t *output_length);

int ultrawidelock_cred_build_auth1_command_ex(uint8_t command_parameters,
				     const uint8_t signature[ULTRAWIDELOCK_CRED_SIGNATURE_SIZE],
				     const uint8_t *reader_certificate,
				     size_t reader_certificate_length, uint8_t *output,
				     size_t output_capacity, size_t *output_length);

int ultrawidelock_cred_parse_auth1_plaintext(const uint8_t *plaintext, size_t plaintext_length,
				    int public_key_requested,
				    struct ultrawidelock_cred_auth1_response *result);

#ifdef __cplusplus
}
#endif
