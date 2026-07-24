<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/ble_message.c`

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `int woz_aliro_ble_parse_message(const uint8_t *data, size_t data_length, struct woz_aliro_ble_message *message, size_t *consumed)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:7`

Parse exactly one message at the beginning of data. consumed permits an
L2CAP SDU containing several concatenated Aliro messages.

### `int woz_aliro_ble_parse_initiate_access(const struct woz_aliro_ble_message *message, const uint8_t **proprietary_information, size_t *proprietary_information_length)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:55`

Parse Notification/Initiate Access Protocol and return the complete encoded
A5 Proprietary Information TLV carried by attribute 0.

### `int woz_aliro_ble_is_uwb_control_message(const struct woz_aliro_ble_message *message)`
`modules/woz_aliro_stack/src/protocol/ble_message.c:82`

True for messages owned by the BLE/UWB adapter after Access Protocol
completion. Keep this narrow so unrelated notifications and third-party
payloads cannot enter the ranging state machine.

### `int woz_aliro_ble_build_access_completed(uint8_t reader_capabilities, uint8_t reader_state, uint8_t output[8])`
`modules/woz_aliro_stack/src/protocol/ble_message.c:110`

Build the unencrypted forms. The caller applies BleSK protection to the
payload and adjusts the length before transmission.

**calls** `build_status`

<details><summary>Undocumented (3)</summary>

- `woz_aliro_ble_build_message` — tested: aliro ble
- `build_status`
- `woz_aliro_ble_build_reader_status_changed` — tested: aliro ble

</details>
