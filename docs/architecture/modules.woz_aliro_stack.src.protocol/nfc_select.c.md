<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/nfc_select.c`

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `int woz_aliro_build_select_command(enum woz_aliro_select_phase phase, uint8_t out[WOZ_ALIRO_SELECT_COMMAND_SIZE])`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:27`

Build 00 A4 04 00 09 <AID> 00 (short case-4 SELECT by DF name).

**calls** `aid_for_phase`

### `int woz_aliro_parse_proprietary_information(const uint8_t *encoded, size_t encoded_length, enum woz_aliro_select_phase phase, struct woz_aliro_select_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:108`

Parse a complete encoded A5 Proprietary Information TLV. This is also the
value carried by BLE Initiate Access Protocol, without an NFC FCI wrapper.

**called by** `woz_aliro_parse_select_response_ex`  ·  **calls** `aid_for_phase`, `parse_proprietary_information`

### `int woz_aliro_parse_select_response(const uint8_t *response, size_t response_length, enum woz_aliro_select_phase phase, uint16_t *selected_protocol_version)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:130`

Parse the complete response APDU, including the trailing SW1/SW2.

**calls** `woz_aliro_parse_select_response_ex`

<details><summary>Undocumented (3)</summary>

- `aid_for_phase`
- `parse_proprietary_information`
- `woz_aliro_parse_select_response_ex`

</details>
