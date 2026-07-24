#include "tlv.h"

#include <string.h>

#include <limits.h>

int woz_aliro_tlv_next(const uint8_t *data, size_t data_length, size_t *offset,
		       struct woz_aliro_tlv *out)
{
	if (data == NULL || offset == NULL || out == NULL || *offset > data_length) {
		return WOZ_ALIRO_TLV_INVALID;
	}
	if (*offset == data_length) {
		return WOZ_ALIRO_TLV_END;
	}

	const size_t start = *offset;
	size_t cursor = start;
	uint32_t tag = data[cursor++];
	if ((tag & 0x1fu) == 0x1fu) {
		unsigned int tag_octets = 1;
		uint8_t octet;
		do {
			if (cursor >= data_length || tag_octets == sizeof(tag)) {
				return WOZ_ALIRO_TLV_INVALID;
			}
			octet = data[cursor++];
			/* DER high-tag-number form must not start with a zero group. */
			if (tag_octets == 1 && (octet & 0x7fu) == 0) {
				return WOZ_ALIRO_TLV_INVALID;
			}
			tag = (tag << 8) | octet;
			++tag_octets;
		} while ((octet & 0x80u) != 0);
	}

	if (cursor >= data_length) {
		return WOZ_ALIRO_TLV_INVALID;
	}
	const uint8_t first_length = data[cursor++];
	size_t value_length = 0;
	if ((first_length & 0x80u) == 0) {
		value_length = first_length;
	} else {
		const unsigned int length_octets = first_length & 0x7fu;
		if (length_octets == 0 || length_octets > sizeof(size_t) ||
		    cursor + length_octets > data_length || data[cursor] == 0) {
			return WOZ_ALIRO_TLV_INVALID;
		}
		for (unsigned int i = 0; i < length_octets; ++i) {
			if (value_length > (SIZE_MAX >> 8)) {
				return WOZ_ALIRO_TLV_INVALID;
			}
			value_length = (value_length << 8) | data[cursor++];
		}
		/* DER requires short form for lengths below 128. */
		if (value_length < 128) {
			return WOZ_ALIRO_TLV_INVALID;
		}
	}

	if (value_length > data_length - cursor) {
		return WOZ_ALIRO_TLV_INVALID;
	}
	out->tag = tag;
	out->value = data + cursor;
	out->length = value_length;
	out->encoded_length = cursor + value_length - start;
	*offset = cursor + value_length;
	return WOZ_ALIRO_TLV_OK;
}

static size_t tag_size(uint32_t tag)
{
	if (tag <= 0xff) {
		return 1;
	}
	if (tag <= 0xffff) {
		return 2;
	}
	if (tag <= 0xffffff) {
		return 3;
	}
	return 0;
}

static size_t length_size(size_t length)
{
	if (length < 0x80) {
		return 1;
	}
	if (length <= 0xff) {
		return 2;
	}
	if (length <= 0xffff) {
		return 3;
	}
	return 0;
}

size_t woz_aliro_tlv_encoded_size(uint32_t tag, size_t value_length)
{
	const size_t encoded_tag_size = tag_size(tag);
	const size_t encoded_length_size = length_size(value_length);
	if (encoded_tag_size == 0 || encoded_length_size == 0 ||
	    value_length > SIZE_MAX - encoded_tag_size - encoded_length_size) {
		return 0;
	}
	return encoded_tag_size + encoded_length_size + value_length;
}

int woz_aliro_tlv_write(uint8_t *data, size_t data_capacity, size_t *offset, uint32_t tag,
			const uint8_t *value, size_t value_length)
{
	if (data == NULL || offset == NULL || (value == NULL && value_length != 0)) {
		return WOZ_ALIRO_TLV_INVALID;
	}
	const size_t total = woz_aliro_tlv_encoded_size(tag, value_length);
	if (total == 0 || *offset > data_capacity || total > data_capacity - *offset) {
		return WOZ_ALIRO_TLV_INVALID;
	}

	const size_t encoded_tag_size = tag_size(tag);
	for (size_t i = 0; i < encoded_tag_size; ++i) {
		data[(*offset)++] = (uint8_t)(tag >> (8 * (encoded_tag_size - i - 1)));
	}
	if (value_length < 0x80) {
		data[(*offset)++] = (uint8_t)value_length;
	} else if (value_length <= 0xff) {
		data[(*offset)++] = 0x81;
		data[(*offset)++] = (uint8_t)value_length;
	} else {
		data[(*offset)++] = 0x82;
		data[(*offset)++] = (uint8_t)(value_length >> 8);
		data[(*offset)++] = (uint8_t)value_length;
	}
	if (value_length != 0) {
		memcpy(data + *offset, value, value_length);
		*offset += value_length;
	}
	return WOZ_ALIRO_TLV_OK;
}
