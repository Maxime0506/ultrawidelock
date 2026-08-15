/* SPDX-License-Identifier: ISC */

/** @file ultrawidelock_uwb_msg_builder.c — big-endian TLV message builder. */

#include "ultrawidelock_uwb_msg_builder.h"

#include "ultrawidelock_alloc.h"
#include <string.h>

/**
 * @brief Releases a message allocated by this layer's message builders.
 *
 * Here rather than in ultrawidelock_uwb_msg.c so that allocating and freeing a message
 * are one translation unit. A caller that only builds messages, which is what
 * the device-side codec in ultrawidelock_device_uwb.c is, would otherwise have to link
 * the reader's whole M2/M4 handler and session lifecycle to get this one line.
 */
void ultrawidelock_uwb_msg_free(struct ultrawidelock_uwb_message *message)
{
	qfree(message);
}

void ultrawidelock_uwb_session_message_free(struct ultrawidelock_uwb_message *message)
{
	ultrawidelock_uwb_msg_free(message);
}

/**
 * @brief Allocate a message with room for the given payload length plus header.
 * @param builder Message builder to initialize.
 * @param payload_len Number of payload bytes to reserve, in addition to the header.
 * @return true on successful allocation, false if allocation failed.
 */
bool ultrawidelock_uwb_msg_builder_init(struct ultrawidelock_uwb_msg_builder *builder,
					uint16_t payload_len)
{
	size_t total = ULTRAWIDELOCK_HEADER_LENGTH + payload_len;

	builder->message = qmalloc(sizeof(struct ultrawidelock_uwb_message) + total);
	if (!builder->message) {
		return false;
	}
	builder->message->len = 0;
	builder->capacity = total;
	return true;
}

/**
 * @brief Append the 4-byte header (protocol, id, big-endian payload length).
 * @param builder Message builder to append the header to.
 * @param protocol Protocol identifier byte.
 * @param id Message identifier byte.
 * @param payload_length Payload length, written big-endian.
 */
void ultrawidelock_uwb_msg_builder_header(struct ultrawidelock_uwb_msg_builder *builder,
					  uint8_t protocol, uint8_t id, uint16_t payload_length)
{
	uint8_t *p = builder->message->data;

	p[builder->message->len++] = protocol;
	p[builder->message->len++] = id;
	p[builder->message->len++] = (uint8_t)(payload_length >> 8);
	p[builder->message->len++] = (uint8_t)payload_length;
}

/**
 * @brief Append id + length + raw value bytes, refusing to overrun the allocation.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param length Length of the attribute value in bytes.
 * @param value Pointer to the attribute value bytes to copy, may be NULL if length is 0.
 * @return true if the attribute was appended, false if it would overrun the builder's capacity.
 */
static bool add_attribute(struct ultrawidelock_uwb_msg_builder *builder, uint8_t id, uint8_t length,
			  const uint8_t *value)
{
	uint8_t *p = builder->message->data;

	if (builder->message->len + ULTRAWIDELOCK_ATTRIBUTE_HEADER_LENGTH + length > builder->capacity) {
		return false;
	}

	p[builder->message->len++] = id;
	p[builder->message->len++] = length;
	if (length && value) {
		memcpy(&p[builder->message->len], value, length);
		builder->message->len += length;
	}
	return true;
}

/**
 * @brief Append a 1-byte-value attribute.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param value 1-byte value to append.
 * @return true if the attribute was appended, false if it would overrun the builder's capacity.
 */
bool ultrawidelock_uwb_msg_builder_add_u8(struct ultrawidelock_uwb_msg_builder *builder, uint8_t id,
					  uint8_t value)
{
	return add_attribute(builder, id, sizeof(value), &value);
}

/**
 * @brief Append a 2-byte big-endian attribute.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param value 16-bit value to append, encoded big-endian.
 * @return true if the attribute was appended, false if it would overrun the builder's capacity.
 */
bool ultrawidelock_uwb_msg_builder_add_u16(struct ultrawidelock_uwb_msg_builder *builder,
					   uint8_t id, uint16_t value)
{
	uint8_t buf[2] = {(uint8_t)(value >> 8), (uint8_t)value};

	return add_attribute(builder, id, sizeof(buf), buf);
}

/**
 * @brief Append a 4-byte big-endian attribute.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param value 32-bit value to append, encoded big-endian.
 * @return true if the attribute was appended, false if it would overrun the builder's capacity.
 */
bool ultrawidelock_uwb_msg_builder_add_u32(struct ultrawidelock_uwb_msg_builder *builder,
					   uint8_t id, uint32_t value)
{
	uint8_t buf[4] = {
		(uint8_t)(value >> 24),
		(uint8_t)(value >> 16),
		(uint8_t)(value >> 8),
		(uint8_t)value,
	};

	return add_attribute(builder, id, sizeof(buf), buf);
}

/**
 * @brief Append a 8-byte big-endian attribute.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param value 64-bit value to append, encoded big-endian.
 * @return true if the attribute was appended, false if it would overrun the builder's capacity.
 */
bool ultrawidelock_uwb_msg_builder_add_u64(struct ultrawidelock_uwb_msg_builder *builder,
					   uint8_t id, uint64_t value)
{
	uint8_t buf[8] = {
		(uint8_t)(value >> 56), (uint8_t)(value >> 48), (uint8_t)(value >> 40),
		(uint8_t)(value >> 32), (uint8_t)(value >> 24), (uint8_t)(value >> 16),
		(uint8_t)(value >> 8),  (uint8_t)value,
	};

	return add_attribute(builder, id, sizeof(buf), buf);
}

/**
 * @brief Append an attribute whose value is count big-endian 16-bit words.
 * @param builder Message builder to append the attribute to.
 * @param id Attribute identifier byte.
 * @param count Number of 16-bit words in the values array.
 * @param values Pointer to the array of 16-bit values to append.
 * @return true if the attribute was appended, false if count is 0, values is NULL, or the attribute
 * would overrun the builder's capacity.
 */
bool ultrawidelock_uwb_msg_builder_add_u16_array(struct ultrawidelock_uwb_msg_builder *builder,
						 uint8_t id, size_t count, const uint16_t *values)
{
	size_t i;

	if (count == 0 || !values) {
		return false;
	}

	uint8_t buf[2 * count];

	for (i = 0; i < count; i++) {
		buf[2 * i] = (uint8_t)(values[i] >> 8);
		buf[2 * i + 1] = (uint8_t)values[i];
	}
	return add_attribute(builder, id, sizeof(buf), buf);
}

/**
 * @brief Append an attribute whose value is count raw bytes.
 */
bool ultrawidelock_uwb_msg_builder_add_bytes(struct ultrawidelock_uwb_msg_builder *builder,
					     uint8_t id, size_t count, const uint8_t *values)
{
	return add_attribute(builder, id, (uint8_t)count, values);
}
