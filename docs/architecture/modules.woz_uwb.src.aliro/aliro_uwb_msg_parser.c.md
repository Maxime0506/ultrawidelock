<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`

@file aliro_uwb_msg_parser.c — TLV attribute parser and big-endian reads.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](aliro_uwb_msg_parser.h.md)

## API

### `uint8_t aliro_uwb_msg_protocol_header(const uint8_t *bytes)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:23`

@brief Extracts the protocol type from byte 0 of an Aliro message header.
@param bytes Pointer to the start of the raw message bytes.
@return The protocol type byte.

### `uint8_t aliro_uwb_msg_message_id(const uint8_t *bytes)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:34`

@brief Extracts the message type ID from byte 1 of an Aliro message header, used to dispatch
M1-M4 setup and ranging messages during parsing.
@param bytes Pointer to the start of the raw message bytes.
@return The message ID byte.

### `uint16_t aliro_uwb_msg_payload_length(const uint8_t *bytes)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:45`

@brief Extracts the payload length from bytes 2-3 of an Aliro message header as a 16-bit
big-endian integer.
@param bytes Pointer to the start of the raw message bytes.
@return The payload length in bytes.

### `struct aliro_uwb_msg_attribute *aliro_uwb_msg_next_attribute(struct aliro_uwb_msg_parser *parser)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:56`

@brief Parses the next TLV attribute from the message payload; returns NULL if offset
exceeds declared message length, clamping to prevent overrun.
@param parser Parser cursor to advance.
@return The next attribute, or NULL at end-of-payload or on overrun.

### `static bool read_be(const struct aliro_uwb_msg_attribute *attr, const char *name, uint8_t width, uint64_t *out)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:89`

@brief Decodes a big-endian fixed-width integer from an attribute; returns false if declared
length does not match width or on parse error.
@param attr Attribute whose value bytes are decoded.
@param name Attribute name, used for error logging.
@param width Expected byte width of the encoded integer.
@param out Destination for the decoded value.
@return true on success, false if the attribute's declared length does not match width.

**called by** `aliro_uwb_msg_read_u16`, `aliro_uwb_msg_read_u32`, `aliro_uwb_msg_read_u64`, `aliro_uwb_msg_read_u8`

### `bool aliro_uwb_msg_read_u8(const struct aliro_uwb_msg_attribute *attr, const char *name, uint8_t *out)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:115`

@brief Decodes an 8-bit big-endian integer from an attribute; returns false on width mismatch or
parse error.
@param attr Attribute whose value bytes are decoded.
@param name Attribute name, used for error logging.
@param out Destination for the decoded value.
@return true on success, false on a size mismatch.

**calls** `read_be`

### `bool aliro_uwb_msg_read_u16(const struct aliro_uwb_msg_attribute *attr, const char *name, uint16_t *out)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:135`

@brief Decodes a 16-bit big-endian integer from an attribute; returns false on width mismatch or
parse error.
@param attr Attribute whose value bytes are decoded.
@param name Attribute name, used for error logging.
@param out Destination for the decoded value.
@return true on success, false on a size mismatch.

**calls** `read_be`

### `bool aliro_uwb_msg_read_u32(const struct aliro_uwb_msg_attribute *attr, const char *name, uint32_t *out)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:155`

@brief Decodes a 32-bit big-endian integer from an attribute; returns false on width mismatch or
parse error.
@param attr Attribute whose value bytes are decoded.
@param name Attribute name, used for error logging.
@param out Destination for the decoded value.
@return true on success, false on a size mismatch.

**calls** `read_be`

### `bool aliro_uwb_msg_read_u64(const struct aliro_uwb_msg_attribute *attr, const char *name, uint64_t *out)`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c:174`

@brief Decodes a 64-bit big-endian integer from an attribute.
@param attr Attribute holding the encoded value.
@param name Attribute name, used only in the mismatch log line.
@param out Receives the decoded value on success.
@return true on success, false on a width mismatch or parse error.

**calls** `read_be`
