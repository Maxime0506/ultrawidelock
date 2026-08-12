/** @file ultrawidelock_uwb_msg_parser.c — TLV attribute parser and big-endian reads. */

#include "ultrawidelock_uwb_msg_parser.h"

#include "ultrawidelock_uwb_msg.h" /* declares the three header accessors defined below */
#include "ultrawidelock_log.h"

LOG_MODULE_DECLARE(ultrawidelock_cred_uwb, LOG_LEVEL_INF);

/* ---- Header accessors ----------------------------------------------------
 *
 * Here, not in ultrawidelock_uwb_msg.c, so that reading a message header does not pull
 * in the reader's M2/M4 handlers and session lifecycle. The device-side codec
 * (ultrawidelock_device_uwb.c) reads headers and builds messages and does neither of
 * those things.
 */

/**
 * @brief Extracts the protocol type from byte 0 of an Aliro message header.
 * @param bytes Pointer to the start of the raw message bytes.
 * @return The protocol type byte.
 */
uint8_t ultrawidelock_uwb_msg_protocol_header(const uint8_t *bytes)
{
	return bytes[0];
}

/**
 * @brief Extracts the message type ID from byte 1 of an Aliro message header, used to dispatch
 * M1-M4 setup and ranging messages during parsing.
 * @param bytes Pointer to the start of the raw message bytes.
 * @return The message ID byte.
 */
uint8_t ultrawidelock_uwb_msg_message_id(const uint8_t *bytes)
{
	return bytes[1];
}

/**
 * @brief Extracts the payload length from bytes 2-3 of an Aliro message header as a 16-bit
 * big-endian integer.
 * @param bytes Pointer to the start of the raw message bytes.
 * @return The payload length in bytes.
 */
uint16_t ultrawidelock_uwb_msg_payload_length(const uint8_t *bytes)
{
	return (uint16_t)((bytes[2] << 8) | bytes[3]);
}

/**
 * @brief Parses the next TLV attribute from the message payload; returns NULL if offset
 * exceeds declared message length, clamping to prevent overrun.
 * @param parser Parser cursor to advance.
 * @return The next attribute, or NULL at end-of-payload or on overrun.
 */
struct ultrawidelock_uwb_msg_attribute *
ultrawidelock_uwb_msg_next_attribute(struct ultrawidelock_uwb_msg_parser *parser)
{
	struct ultrawidelock_uwb_msg_attribute *attr;

	/* Need the full 2-byte attribute header (id + length) present before
	 * dereferencing attr->length; a lone trailing byte would over-read
	 * parser->data[offset + 1]. Written as an addition (offset <= length is an
	 * invariant here) to avoid a size_t underflow. */
	if (parser->offset + ULTRAWIDELOCK_ATTRIBUTE_HEADER_LENGTH > parser->length) {
		return NULL;
	}

	attr = (struct ultrawidelock_uwb_msg_attribute *)&parser->data[parser->offset];
	parser->offset += ULTRAWIDELOCK_ATTRIBUTE_HEADER_LENGTH + attr->length;

	/* Clamp and stop on a declared length that overruns the payload. */
	if (parser->offset > parser->length) {
		parser->offset = parser->length;
		return NULL;
	}

	return attr;
}

/**
 * @brief Decodes a big-endian fixed-width integer from an attribute; returns false if declared
 * length does not match width or on parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param width Expected byte width of the encoded integer.
 * @param out Destination for the decoded value.
 * @return true on success, false if the attribute's declared length does not match width.
 */
static bool read_be(const struct ultrawidelock_uwb_msg_attribute *attr, const char *name,
		    uint8_t width, uint64_t *out)
{
	uint64_t value = 0;
	uint8_t i;

	if (attr->length != width) {
		LOG_ERR("attr %s: length %u, expected %u", name, attr->length, width);
		return false;
	}

	for (i = 0; i < width; i++) {
		value = (value << 8) | attr->value[i];
	}
	*out = value;
	return true;
}

/**
 * @brief Decodes an 8-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool ultrawidelock_uwb_msg_read_u8(const struct ultrawidelock_uwb_msg_attribute *attr,
				   const char *name, uint8_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint8_t), &value)) {
		return false;
	}
	*out = (uint8_t)value;
	return true;
}

/**
 * @brief Decodes a 16-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool ultrawidelock_uwb_msg_read_u16(const struct ultrawidelock_uwb_msg_attribute *attr,
				    const char *name, uint16_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint16_t), &value)) {
		return false;
	}
	*out = (uint16_t)value;
	return true;
}

/**
 * @brief Decodes a 32-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool ultrawidelock_uwb_msg_read_u32(const struct ultrawidelock_uwb_msg_attribute *attr,
				    const char *name, uint32_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint32_t), &value)) {
		return false;
	}
	*out = (uint32_t)value;
	return true;
}

/**
 * @brief Decodes a 64-bit big-endian integer from an attribute.
 * @param attr Attribute holding the encoded value.
 * @param name Attribute name, used only in the mismatch log line.
 * @param out Receives the decoded value on success.
 * @return true on success, false on a width mismatch or parse error.
 */
bool ultrawidelock_uwb_msg_read_u64(const struct ultrawidelock_uwb_msg_attribute *attr,
				    const char *name, uint64_t *out)
{
	return read_be(attr, name, sizeof(uint64_t), out);
}
