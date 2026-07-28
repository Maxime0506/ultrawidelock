<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/ble_message.c`

@file ble_message.c
BLE protocol message framing: parse and build protocol/message_id headers and payloads; parse and
extract Initiate Access, UWB control, Access Completed, and Reader Status Changed messages.

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `int woz_aliro_ble_parse_message(const uint8_t *data, size_t data_length, struct woz_aliro_ble_message *message, size_t *consumed)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:12`

Parse exactly one message at the beginning of data. consumed permits an
L2CAP SDU containing several concatenated Aliro messages.

### `int woz_aliro_ble_build_message(uint8_t protocol, uint8_t message_id, const uint8_t *payload, size_t payload_length, uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:45`

Build a BLE protocol message with a given payload. Output is header (4 bytes: protocol,
message_id, payload length as big-endian uint16) followed by the payload. Caller must provide a
buffer large enough for header + payload; protocol field must not set bits 7-6. Payload must be
1–65535 bytes.

### `int woz_aliro_ble_parse_initiate_access(const struct woz_aliro_ble_message *message, const uint8_t **proprietary_information, size_t *proprietary_information_length)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:66`

Parse Notification/Initiate Access Protocol and return the complete encoded
A5 Proprietary Information TLV carried by attribute 0.

### `int woz_aliro_ble_is_uwb_control_message(const struct woz_aliro_ble_message *message)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:93`

True for messages owned by the BLE/UWB adapter after Access Protocol
completion. Keep this narrow so unrelated notifications and third-party
payloads cannot enter the ranging state machine.

### `static int build_status(uint8_t message_id, uint8_t first, uint8_t reader_state, uint8_t output[8])`
`modules/woz_aliro_stack/src/protocol/ble_message.c:110`

Build a BLE Status message into an 8-byte array. Output is always 8 bytes when successful.
Encodes protocol/message_id header, attribute ID 0 (State/Reader Information), and two state
bytes (first and reader_state); output capacity must be >= 8 bytes.

**called by** `woz_aliro_ble_build_access_completed`, `woz_aliro_ble_build_reader_status_changed`

### `int woz_aliro_ble_build_access_completed(uint8_t reader_capabilities, uint8_t reader_state, uint8_t output[8])`
`modules/woz_aliro_stack/src/protocol/ble_message.c:126`

Build the unencrypted forms. The caller applies BleSK protection to the
payload and adjusts the length before transmission.

**calls** `build_status`

### `int woz_aliro_ble_build_reader_status_changed(uint8_t operation_source, uint8_t reader_state, uint8_t output[8])`
`modules/woz_aliro_stack/src/protocol/ble_message.c:137`

Build an 8-byte unencrypted BLE Reader Status Changed notification with operation_source and
reader_state; return 0 on success, -1 on error.

**calls** `build_status`
