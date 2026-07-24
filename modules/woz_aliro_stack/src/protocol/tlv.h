/* Minimal strict BER/DER-TLV reader for Aliro APDU payloads. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct woz_aliro_tlv {
	uint32_t tag;
	const uint8_t *value;
	size_t length;
	size_t encoded_length;
};

enum woz_aliro_tlv_result {
	WOZ_ALIRO_TLV_OK = 0,
	WOZ_ALIRO_TLV_END = 1,
	WOZ_ALIRO_TLV_INVALID = -1,
};

/* Parse one TLV at *offset and advance the offset past it. Indefinite and
 * non-minimal DER lengths are rejected. */
int woz_aliro_tlv_next(const uint8_t *data, size_t data_length, size_t *offset,
		       struct woz_aliro_tlv *out);

/* Return the encoded size of a definite-length TLV, or zero when the tag or
 * length cannot be represented by this codec. Tags are supplied in their
 * normal big-endian encoded form (for example 0x7f66). */
size_t woz_aliro_tlv_encoded_size(uint32_t tag, size_t value_length);

/* Append one definite-length TLV at *offset. */
int woz_aliro_tlv_write(uint8_t *data, size_t data_capacity, size_t *offset, uint32_t tag,
			const uint8_t *value, size_t value_length);

#ifdef __cplusplus
}
#endif
