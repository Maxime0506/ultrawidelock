<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`

@file aliro_uwb_msg_parser.h — TLV attribute iteration and big-endian reads.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](aliro_device_uwb.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](aliro_uwb_msg_parser.c.md)

## API

### `struct aliro_uwb_msg_attribute`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h:19`

@brief A single type/length/value attribute overlaid on message bytes.
@param id Attribute identifier.
@param length Length of value in bytes.
@param value Variable-length attribute value.

### `struct aliro_uwb_msg_parser`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h:31`

@brief Cursor walking the attributes of one message payload.
@param length Total length of the message in bytes.
@param offset Current parse offset in bytes.
@param data Message bytes being walked.
