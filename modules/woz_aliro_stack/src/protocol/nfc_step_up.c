/**
 * @file nfc_step_up.c
 * NFC step-up messaging: compact-key CBOR encoder/decoder for Aliro DeviceRequest and SessionData
 * (ISO 18013-5). Core: put appends to writer buffer; cbor_head / cbor_bytes / text build encoded
 * items; cbor_read_head parses with validation (non-minimal representation rejected);
 * build_device_request constructs DeviceRequest (compact keys); wrap_session_data /
 * unwrap_session_data encode/decode SessionData; wrap_do53 / unwrap_do53 TLV-wrap messages;
 * build_envelope_command / build_get_response_command and collect_response chain ISO APDU commands.
 */
#include "nfc_step_up.h"

#include "tlv.h"

#include <string.h>

/**
 * Buffered writer for composing CBOR/TLV payloads: tracks current write position and capacity. Used
 * by message builders to accumulate encoded data.
 */
struct writer {
	uint8_t *data;
	size_t capacity;
	size_t length;
};

/**
 * Append bytes to a writer buffer. Returns WOZ_ALIRO_STEP_UP_OK on success,
 * WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL if appending would exceed capacity. Length-zero writes are
 * allowed and do not alter the buffer.
 */
static int put(struct writer *w, const void *data, size_t length)
{
	if (length > w->capacity - w->length) {
		return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
	}
	if (length != 0) {
		memcpy(w->data + w->length, data, length);
	}
	w->length += length;
	return WOZ_ALIRO_STEP_UP_OK;
}

/**
 * Encode a CBOR head (major type + argument value) into the writer buffer. Values <24 fit in 1
 * byte; 24–255 in 2 bytes; 256–65535 in 3 bytes; 65536–2^32−1 in 5 bytes; larger in 9 bytes. Return
 * WOZ_ALIRO_STEP_UP_OK on success, WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL if appending would overflow.
 */
static int cbor_head(struct writer *w, uint8_t major, size_t value)
{
	uint8_t encoded[9];
	size_t n = 1;
	if (value < 24) {
		encoded[0] = (uint8_t)((major << 5) | value);
	} else if (value <= 0xff) {
		encoded[0] = (uint8_t)((major << 5) | 24);
		encoded[1] = (uint8_t)value;
		n = 2;
	} else if (value <= 0xffff) {
		encoded[0] = (uint8_t)((major << 5) | 25);
		encoded[1] = (uint8_t)(value >> 8);
		encoded[2] = (uint8_t)value;
		n = 3;
	} else if (value <= 0xffffffffu) {
		encoded[0] = (uint8_t)((major << 5) | 26);
		for (size_t i = 0; i < 4; ++i) {
			encoded[1 + i] = (uint8_t)(value >> (24 - 8 * i));
		}
		n = 5;
	} else {
		encoded[0] = (uint8_t)((major << 5) | 27);
		for (size_t i = 0; i < 8; ++i) {
			encoded[1 + i] = (uint8_t)(value >> (56 - 8 * i));
		}
		n = 9;
	}
	return put(w, encoded, n);
}

/**
 * Encode a CBOR byte string (major type 2 or 3) or text string: emit the head (type + length) then
 * the payload bytes. Return WOZ_ALIRO_STEP_UP_OK on success, WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL if
 * appending would overflow.
 */
static int cbor_bytes(struct writer *w, uint8_t major, const uint8_t *data, size_t length)
{
	int rc = cbor_head(w, major, length);
	return rc == 0 ? put(w, data, length) : rc;
}

/**
 * Encode a UTF-8 text string as CBOR major type 3: emit the head and the string bytes. Return
 * WOZ_ALIRO_STEP_UP_OK on success, WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL if appending would overflow.
 */
static int text(struct writer *w, const char *value)
{
	return cbor_bytes(w, 3, (const uint8_t *)value, strlen(value));
}

int woz_aliro_build_device_request(const uint8_t *element_identifier,
				   size_t element_identifier_length, bool intent_to_store,
				   uint8_t *output, size_t output_capacity, size_t *output_length)
{
	if (element_identifier == NULL || element_identifier_length == 0 || output == NULL ||
	    output_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	uint8_t items[512];
	struct writer item = {items, sizeof(items), 0};
	int rc;
#define W(call)                                                                                    \
	do {                                                                                       \
		rc = (call);                                                                       \
		if (rc != 0)                                                                       \
			return rc;                                                                 \
	} while (0)
	W(cbor_head(&item, 5, 2));
	W(text(&item, "1"));
	W(cbor_head(&item, 5, 1));
	W(text(&item, "aliro-a"));
	W(cbor_head(&item, 5, 1));
	W(cbor_bytes(&item, 3, element_identifier, element_identifier_length));
	const uint8_t boolean = intent_to_store ? 0xf5 : 0xf4;
	W(put(&item, &boolean, 1));
	W(text(&item, "5"));
	W(text(&item, "aliro-a"));

	struct writer out = {output, output_capacity, 0};
	W(cbor_head(&out, 5, 2));
	W(text(&out, "1"));
	W(text(&out, "1.0"));
	W(text(&out, "2"));
	W(cbor_head(&out, 4, 1));
	W(cbor_head(&out, 5, 1));
	W(text(&out, "1"));
	W(cbor_head(&out, 6, 24));
	W(cbor_bytes(&out, 2, items, item.length));
	*output_length = out.length;
#undef W
	return WOZ_ALIRO_STEP_UP_OK;
}

int woz_aliro_wrap_session_data(const uint8_t *ciphertext, size_t ciphertext_length,
				uint8_t *output, size_t output_capacity, size_t *output_length)
{
	if ((ciphertext == NULL && ciphertext_length != 0) || output == NULL ||
	    output_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	struct writer w = {output, output_capacity, 0};
	int rc = cbor_head(&w, 5, 1);
	if (rc == 0) {
		rc = text(&w, "data");
	}
	if (rc == 0) {
		rc = cbor_bytes(&w, 2, ciphertext, ciphertext_length);
	}
	if (rc == 0) {
		*output_length = w.length;
	}
	return rc;
}

/**
 * Parse a CBOR major type and initial value from an encoded stream. Major type is encoded in the
 * top 3 bits of the first byte; additional info determines whether the value follows. Validates
 * encoding: rejects non-minimal representations (e.g., a 1-byte value encoded with 2 bytes) and
 * returns WOZ_ALIRO_STEP_UP_INVALID_DATA if the major type or additional-info field does not match
 * expectations.
 */
static int cbor_read_head(const uint8_t *data, size_t length, size_t *offset, uint8_t major,
			  size_t *value)
{
	if (*offset >= length) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	uint8_t b = data[(*offset)++];
	if ((b >> 5) != major) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	uint8_t ai = b & 31;
	if (ai < 24) {
		*value = ai;
		return 0;
	}
	size_t n = ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : ai == 27 ? 8 : 0;
	if (n == 0 || n > sizeof(size_t) || n > length - *offset) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	size_t v = 0;
	for (size_t i = 0; i < n; ++i) {
		v = (v << 8) | data[(*offset)++];
	}
	if ((n == 1 && v < 24) || (n == 2 && v <= 0xff) || (n == 4 && v <= 0xffff)) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	*value = v;
	return 0;
}

/**
 * Unwrap an ISO 18013-5 SessionData envelope (CBOR map with "data" key) to extract the encrypted
 * ciphertext. Validate that SessionData is a map with exactly one entry and return a pointer into
 * the input buffer for the ciphertext bytes and their length. Return 0 on success,
 * WOZ_ALIRO_STEP_UP_INVALID_DATA on format error or invalid argument.
 */
int woz_aliro_unwrap_session_data(const uint8_t *session_data, size_t session_data_length,
				  const uint8_t **ciphertext, size_t *ciphertext_length)
{
	if (session_data == NULL || ciphertext == NULL || ciphertext_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	size_t off = 0, n;
	if (cbor_read_head(session_data, session_data_length, &off, 5, &n) != 0 || n != 1 ||
	    cbor_read_head(session_data, session_data_length, &off, 3, &n) != 0 || n != 4 ||
	    n > session_data_length - off || memcmp(session_data + off, "data", 4) != 0) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	off += 4;
	if (cbor_read_head(session_data, session_data_length, &off, 2, &n) != 0 ||
	    n > session_data_length - off || off + n != session_data_length) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	*ciphertext = session_data + off;
	*ciphertext_length = n;
	return 0;
}

int woz_aliro_wrap_do53(const uint8_t *message, size_t message_length, uint8_t *output,
			size_t output_capacity, size_t *output_length)
{
	if ((message == NULL && message_length != 0) || output == NULL || output_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	size_t off = 0;
	if (woz_aliro_tlv_write(output, output_capacity, &off, 0x53, message, message_length) !=
	    WOZ_ALIRO_TLV_OK) {
		return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
	}
	*output_length = off;
	return 0;
}

/**
 * Unwrap an Aliro DO'53 TLV-encoded message: extract the value of a tag-0x53 object from an encoded
 * buffer. Returns a pointer into the input buffer for the message payload and its length; the
 * pointer is valid only as long as the input buffer is valid.
 */
int woz_aliro_unwrap_do53(const uint8_t *encoded, size_t encoded_length, const uint8_t **message,
			  size_t *message_length)
{
	if (encoded == NULL || message == NULL || message_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	struct woz_aliro_tlv tlv;
	size_t off = 0;
	if (woz_aliro_tlv_next(encoded, encoded_length, &off, &tlv) != 0 || tlv.tag != 0x53 ||
	    off != encoded_length) {
		return WOZ_ALIRO_STEP_UP_INVALID_DATA;
	}
	*message = tlv.value;
	*message_length = tlv.length;
	return 0;
}

int woz_aliro_build_envelope_command(const uint8_t *encoded_do53, size_t encoded_length,
				     size_t *offset, size_t max_command_data,
				     size_t max_response_data, bool extended_supported,
				     uint8_t *output, size_t output_capacity, size_t *output_length,
				     bool *last_fragment)
{
	if (encoded_do53 == NULL || offset == NULL || *offset >= encoded_length ||
	    max_command_data == 0 || max_command_data > 65535 || max_response_data == 0 ||
	    max_response_data > 65536 || output == NULL || output_length == NULL ||
	    last_fragment == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	size_t fragment = encoded_length - *offset;
	if (fragment > max_command_data) {
		fragment = max_command_data;
	}
	*last_fragment = *offset + fragment == encoded_length;
	const bool extended = fragment > 255 || (*last_fragment && max_response_data > 256);
	if (extended && !extended_supported) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	size_t needed = (extended ? 7 : 5) + fragment + (*last_fragment ? (extended ? 2 : 1) : 0);
	if (needed > output_capacity) {
		return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
	}
	size_t o = 0;
	output[o++] = *last_fragment ? 0x00 : 0x10;
	output[o++] = 0xc3;
	output[o++] = 0x00;
	output[o++] = 0x00;
	if (extended) {
		output[o++] = 0;
		output[o++] = (uint8_t)(fragment >> 8);
		output[o++] = (uint8_t)fragment;
	} else {
		output[o++] = (uint8_t)fragment;
	}
	memcpy(output + o, encoded_do53 + *offset, fragment);
	o += fragment;
	if (*last_fragment) {
		if (extended) {
			uint16_t le = max_response_data == 65536 ? 0 : (uint16_t)max_response_data;
			output[o++] = (uint8_t)(le >> 8);
			output[o++] = (uint8_t)le;
		} else {
			output[o++] = max_response_data == 256 ? 0 : (uint8_t)max_response_data;
		}
	}
	*offset += fragment;
	*output_length = o;
	return 0;
}

/**
 * Build an ISO 7816 GET RESPONSE command APDU for retrieving data from a previous response chain.
 * For lengths <= 256 bytes, outputs a 5-byte APDU; for lengths > 256, outputs a 7-byte
 * extended-length APDU. Output capacity must be >= 5 or >= 7 bytes respectively. Expected length
 * must be 1–65536.
 */
int woz_aliro_build_get_response_command(size_t expected_length, uint8_t *output,
					 size_t output_capacity, size_t *output_length)
{
	if (expected_length == 0 || expected_length > 65536 || output == NULL ||
	    output_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	if (expected_length <= 256) {
		if (output_capacity < 5) {
			return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
		}
		const uint8_t apdu[] = {0x00, 0xc0, 0x00, 0x00,
					expected_length == 256 ? 0 : (uint8_t)expected_length};
		memcpy(output, apdu, sizeof(apdu));
		*output_length = sizeof(apdu);
	} else {
		if (output_capacity < 7) {
			return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
		}
		uint16_t le = expected_length == 65536 ? 0 : (uint16_t)expected_length;
		const uint8_t apdu[] = {0x00,       0xc0, 0x00, 0x00, 0x00, (uint8_t)(le >> 8),
					(uint8_t)le};
		memcpy(output, apdu, sizeof(apdu));
		*output_length = sizeof(apdu);
	}
	return 0;
}

int woz_aliro_collect_response(const uint8_t *response, size_t response_length, uint8_t *collected,
			       size_t collected_capacity, size_t *collected_length,
			       size_t *next_length)
{
	if (response == NULL || response_length < 2 || collected == NULL ||
	    collected_length == NULL || *collected_length > collected_capacity ||
	    next_length == NULL) {
		return WOZ_ALIRO_STEP_UP_INVALID_ARGUMENT;
	}
	size_t data_length = response_length - 2;
	if (data_length > collected_capacity - *collected_length) {
		return WOZ_ALIRO_STEP_UP_BUFFER_TOO_SMALL;
	}
	uint8_t sw1 = response[data_length], sw2 = response[data_length + 1];
	if (!((sw1 == 0x90 && sw2 == 0) || sw1 == 0x61)) {
		return WOZ_ALIRO_STEP_UP_STATUS_ERROR;
	}
	memcpy(collected + *collected_length, response, data_length);
	*collected_length += data_length;
	if (sw1 == 0x61) {
		*next_length = sw2 == 0 ? 256 : sw2;
		return WOZ_ALIRO_STEP_UP_MORE_RESPONSE;
	}
	*next_length = 0;
	return WOZ_ALIRO_STEP_UP_OK;
}
