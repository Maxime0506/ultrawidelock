#include "nfc_auth.h"

#include "tlv.h"

#include <stdbool.h>
#include <string.h>

static int put_tlv(uint8_t *output, size_t capacity, size_t *offset, uint32_t tag,
		   const uint8_t *value, size_t length)
{
	return woz_aliro_tlv_write(output, capacity, offset, tag, value, length) == WOZ_ALIRO_TLV_OK
		       ? WOZ_ALIRO_AUTH_OK
		       : WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
}

int woz_aliro_build_auth0_command(const struct woz_aliro_auth0_command *params, uint8_t *output,
				  size_t output_capacity, size_t *output_length)
{
	if (params == NULL || output == NULL || output_length == NULL ||
	    params->reader_ephemeral_public_key == NULL || params->transaction_identifier == NULL ||
	    params->reader_identifier == NULL ||
	    (params->vendor_extension == NULL && params->vendor_extension_length != 0) ||
	    params->reader_ephemeral_public_key[0] != 0x04 ||
	    (params->command_parameters & 0xfe) != 0 || params->authentication_policy == 0 ||
	    params->authentication_policy > 3 || params->protocol_version != 0x0100) {
		return WOZ_ALIRO_AUTH_INVALID_ARGUMENT;
	}
	const size_t body_length =
		129 + (params->vendor_extension_length == 0
			       ? 0
			       : woz_aliro_tlv_encoded_size(0xb1, params->vendor_extension_length));
	if (body_length > 255 || output_capacity < body_length + 6) {
		return WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
	}

	output[0] = 0x80;
	output[1] = 0x80;
	output[2] = 0x00;
	output[3] = 0x00;
	output[4] = (uint8_t)body_length;
	size_t offset = 5;
	uint8_t version[] = {(uint8_t)(params->protocol_version >> 8),
			     (uint8_t)params->protocol_version};
	if (put_tlv(output, output_capacity, &offset, 0x41, &params->command_parameters, 1) ||
	    put_tlv(output, output_capacity, &offset, 0x42, &params->authentication_policy, 1) ||
	    put_tlv(output, output_capacity, &offset, 0x5c, version, sizeof(version)) ||
	    put_tlv(output, output_capacity, &offset, 0x87, params->reader_ephemeral_public_key,
		    WOZ_ALIRO_PUBLIC_KEY_SIZE) ||
	    put_tlv(output, output_capacity, &offset, 0x4c, params->transaction_identifier,
		    WOZ_ALIRO_TRANSACTION_ID_SIZE) ||
	    put_tlv(output, output_capacity, &offset, 0x4d, params->reader_identifier,
		    WOZ_ALIRO_READER_ID_SIZE) ||
	    (params->vendor_extension_length != 0 &&
	     put_tlv(output, output_capacity, &offset, 0xb1, params->vendor_extension,
		     params->vendor_extension_length))) {
		return WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
	}
	output[offset++] = 0x00;
	*output_length = offset;
	return WOZ_ALIRO_AUTH_OK;
}

int woz_aliro_parse_auth0_response(const uint8_t *response, size_t response_length,
				   int fast_requested, struct woz_aliro_auth0_response *result)
{
	if (response == NULL || result == NULL) {
		return WOZ_ALIRO_AUTH_INVALID_ARGUMENT;
	}
	memset(result, 0, sizeof(*result));
	if (response_length < 2) {
		return WOZ_ALIRO_AUTH_INVALID_APDU;
	}
	if (response[response_length - 2] != 0x90 || response[response_length - 1] != 0x00) {
		return WOZ_ALIRO_AUTH_STATUS_ERROR;
	}

	size_t offset = 0;
	const size_t data_length = response_length - 2;
	struct woz_aliro_tlv tlv;
	if (woz_aliro_tlv_next(response, data_length, &offset, &tlv) != WOZ_ALIRO_TLV_OK ||
	    tlv.tag != 0x86 || tlv.length != WOZ_ALIRO_PUBLIC_KEY_SIZE || tlv.value[0] != 0x04) {
		return WOZ_ALIRO_AUTH_WRONG_CONTENT;
	}
	memcpy(result->credential_ephemeral_public_key, tlv.value, WOZ_ALIRO_PUBLIC_KEY_SIZE);

	if (offset < data_length) {
		if (woz_aliro_tlv_next(response, data_length, &offset, &tlv) != WOZ_ALIRO_TLV_OK) {
			return WOZ_ALIRO_AUTH_INVALID_APDU;
		}
		if (tlv.tag == 0x9d) {
			if (!fast_requested || tlv.length != 64) {
				return WOZ_ALIRO_AUTH_WRONG_CONTENT;
			}
			result->cryptogram = tlv.value;
			result->cryptogram_length = tlv.length;
			if (offset < data_length &&
			    woz_aliro_tlv_next(response, data_length, &offset, &tlv) !=
				    WOZ_ALIRO_TLV_OK) {
				return WOZ_ALIRO_AUTH_INVALID_APDU;
			}
		}
		if (tlv.tag == 0xb2) {
			result->vendor_extension = tlv.value;
			result->vendor_extension_length = tlv.length;
		} else if (offset <= data_length && tlv.tag != 0x9d) {
			return WOZ_ALIRO_AUTH_WRONG_CONTENT;
		}
	}
	if (offset != data_length || (!!result->cryptogram != !!fast_requested)) {
		return WOZ_ALIRO_AUTH_WRONG_CONTENT;
	}
	return WOZ_ALIRO_AUTH_OK;
}

int woz_aliro_build_authentication_data(
	const uint8_t reader_identifier[WOZ_ALIRO_READER_ID_SIZE],
	const uint8_t credential_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE],
	const uint8_t reader_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE],
	const uint8_t transaction_identifier[WOZ_ALIRO_TRANSACTION_ID_SIZE], uint32_t usage,
	uint8_t output[WOZ_ALIRO_AUTH_DATA_SIZE])
{
	if (reader_identifier == NULL || credential_ephemeral_public_key == NULL ||
	    reader_ephemeral_public_key == NULL || transaction_identifier == NULL ||
	    output == NULL || credential_ephemeral_public_key[0] != 0x04 ||
	    reader_ephemeral_public_key[0] != 0x04) {
		return WOZ_ALIRO_AUTH_INVALID_ARGUMENT;
	}
	uint8_t usage_bytes[] = {(uint8_t)(usage >> 24), (uint8_t)(usage >> 16),
				 (uint8_t)(usage >> 8), (uint8_t)usage};
	size_t offset = 0;
	if (put_tlv(output, WOZ_ALIRO_AUTH_DATA_SIZE, &offset, 0x4d, reader_identifier,
		    WOZ_ALIRO_READER_ID_SIZE) ||
	    put_tlv(output, WOZ_ALIRO_AUTH_DATA_SIZE, &offset, 0x86,
		    credential_ephemeral_public_key + 1, 32) ||
	    put_tlv(output, WOZ_ALIRO_AUTH_DATA_SIZE, &offset, 0x87,
		    reader_ephemeral_public_key + 1, 32) ||
	    put_tlv(output, WOZ_ALIRO_AUTH_DATA_SIZE, &offset, 0x4c, transaction_identifier,
		    WOZ_ALIRO_TRANSACTION_ID_SIZE) ||
	    put_tlv(output, WOZ_ALIRO_AUTH_DATA_SIZE, &offset, 0x93, usage_bytes,
		    sizeof(usage_bytes)) ||
	    offset != WOZ_ALIRO_AUTH_DATA_SIZE) {
		return WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
	}
	return WOZ_ALIRO_AUTH_OK;
}

int woz_aliro_build_auth1_command(uint8_t command_parameters,
				  const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE],
				  uint8_t *output, size_t output_capacity, size_t *output_length)
{
	return woz_aliro_build_auth1_command_ex(command_parameters, signature, NULL, 0, output,
						output_capacity, output_length);
}

int woz_aliro_build_auth1_command_ex(uint8_t command_parameters,
				     const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE],
				     const uint8_t *reader_certificate,
				     size_t reader_certificate_length, uint8_t *output,
				     size_t output_capacity, size_t *output_length)
{
	if (signature == NULL || output == NULL || output_length == NULL ||
	    (command_parameters & 0xfe) != 0 ||
	    (reader_certificate == NULL && reader_certificate_length != 0)) {
		return WOZ_ALIRO_AUTH_INVALID_ARGUMENT;
	}
	const size_t certificate_tlv_length =
		reader_certificate_length == 0
			? 0
			: woz_aliro_tlv_encoded_size(0x90, reader_certificate_length);
	const size_t body_length = 69 + certificate_tlv_length;
	if (body_length > 255 || output_capacity < body_length + 6) {
		return WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
	}
	output[0] = 0x80;
	output[1] = 0x81;
	output[2] = 0;
	output[3] = 0;
	output[4] = (uint8_t)body_length;
	size_t offset = 5;
	if (put_tlv(output, output_capacity, &offset, 0x41, &command_parameters, 1) ||
	    put_tlv(output, output_capacity, &offset, 0x9e, signature, WOZ_ALIRO_SIGNATURE_SIZE) ||
	    (reader_certificate_length != 0 &&
	     put_tlv(output, output_capacity, &offset, 0x90, reader_certificate,
		     reader_certificate_length))) {
		return WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL;
	}
	output[offset++] = 0;
	*output_length = offset;
	return offset == body_length + 6 ? WOZ_ALIRO_AUTH_OK : WOZ_ALIRO_AUTH_INVALID_APDU;
}

int woz_aliro_parse_auth1_plaintext(const uint8_t *plaintext, size_t plaintext_length,
				    int public_key_requested,
				    struct woz_aliro_auth1_response *result)
{
	if (plaintext == NULL || result == NULL) {
		return WOZ_ALIRO_AUTH_INVALID_ARGUMENT;
	}
	memset(result, 0, sizeof(*result));
	size_t offset = 0;
	struct woz_aliro_tlv tlv;
	if (woz_aliro_tlv_next(plaintext, plaintext_length, &offset, &tlv) != WOZ_ALIRO_TLV_OK) {
		return WOZ_ALIRO_AUTH_INVALID_APDU;
	}
	if (!public_key_requested || tlv.tag != 0x5a || tlv.length != WOZ_ALIRO_PUBLIC_KEY_SIZE ||
	    tlv.value[0] != 0x04) {
		return WOZ_ALIRO_AUTH_WRONG_CONTENT;
	}
	memcpy(result->credential_public_key, tlv.value, WOZ_ALIRO_PUBLIC_KEY_SIZE);
	if (woz_aliro_tlv_next(plaintext, plaintext_length, &offset, &tlv) != WOZ_ALIRO_TLV_OK ||
	    tlv.tag != 0x9e || tlv.length != WOZ_ALIRO_SIGNATURE_SIZE) {
		return WOZ_ALIRO_AUTH_WRONG_CONTENT;
	}
	memcpy(result->signature, tlv.value, WOZ_ALIRO_SIGNATURE_SIZE);

	bool found_signaling = false;
	while (offset < plaintext_length) {
		if (woz_aliro_tlv_next(plaintext, plaintext_length, &offset, &tlv) !=
		    WOZ_ALIRO_TLV_OK) {
			return WOZ_ALIRO_AUTH_INVALID_APDU;
		}
		if (tlv.tag == 0x4b && !found_signaling) {
			continue;
		}
		if (tlv.tag == 0x5e && !found_signaling && tlv.length == 2) {
			found_signaling = true;
			result->signaling_bitmap = ((uint16_t)tlv.value[0] << 8) | tlv.value[1];
		} else if (tlv.tag == 0x91 && found_signaling &&
			   result->credential_signed_timestamp == NULL && tlv.length == 20) {
			result->credential_signed_timestamp = tlv.value;
		} else if (tlv.tag == 0x92 && found_signaling &&
			   result->revocation_signed_timestamp == NULL && tlv.length == 20) {
			result->revocation_signed_timestamp = tlv.value;
		} else {
			return WOZ_ALIRO_AUTH_WRONG_CONTENT;
		}
	}
	return found_signaling ? WOZ_ALIRO_AUTH_OK : WOZ_ALIRO_AUTH_WRONG_CONTENT;
}
