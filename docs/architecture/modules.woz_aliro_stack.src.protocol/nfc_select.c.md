<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/nfc_select.c`

@file nfc_select.c
NFC SELECT command builder and response parser for Aliro. build_select_command emits 00 A4 04 00
09 <AID> 00. parse_proprietary_information decodes type-0x80 data from a SELECT response,
extracting protocol version (expedited phase only) and extended-length sizes (0x7f66 TLV).
parse_select_response and parse_select_response_ex validate the trailing 9000, check AID, and
call parse_proprietary_information.

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `static const uint8_t *aid_for_phase(enum woz_aliro_select_phase phase)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:28`

Map a SELECT phase (EXPEDITED or STEP_UP) to its corresponding Aliro AID byte sequence; return
NULL if phase is unknown.

**called by** `woz_aliro_build_select_command`, `woz_aliro_parse_proprietary_information`, `woz_aliro_parse_select_response_ex`

### `int woz_aliro_build_select_command(enum woz_aliro_select_phase phase, uint8_t out[WOZ_ALIRO_SELECT_COMMAND_SIZE])`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:39`

Build 00 A4 04 00 09 <AID> 00 (short case-4 SELECT by DF name).

**calls** `aid_for_phase`

### `static int parse_proprietary_information(const uint8_t *data, size_t length, enum woz_aliro_select_phase phase, struct woz_aliro_select_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:59`

Parse proprietary information TLV data from a SELECT response: validate type tag 0x80, extract
protocol version from expedited phase 0x5c, and decode extended-length sizes from 0x7f66 if
present; return WOZ_ALIRO_SELECT_OK on valid parse or error code if format or content is
malformed.

**called by** `woz_aliro_parse_proprietary_information`

### `int woz_aliro_parse_proprietary_information(const uint8_t *encoded, size_t encoded_length, enum woz_aliro_select_phase phase, struct woz_aliro_select_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:126`

Parse a complete encoded A5 Proprietary Information TLV. This is also the
value carried by BLE Initiate Access Protocol, without an NFC FCI wrapper.

**called by** `woz_aliro_parse_select_response_ex`  ·  **calls** `aid_for_phase`, `parse_proprietary_information`

### `int woz_aliro_parse_select_response(const uint8_t *response, size_t response_length, enum woz_aliro_select_phase phase, uint16_t *selected_protocol_version)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:148`

Parse the complete response APDU, including the trailing SW1/SW2.

**calls** `woz_aliro_parse_select_response_ex`

### `int woz_aliro_parse_select_response_ex(const uint8_t *response, size_t response_length, enum woz_aliro_select_phase phase, struct woz_aliro_select_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_select.c:168`

Parse a complete ISO 7816 SELECT response APDU (ending with 9000), validate the AID matches the
requested phase (EXPEDITED or STEP_UP), extract proprietary information TLV, and decode protocol
version and extended-length sizes. Return WOZ_ALIRO_SELECT_OK on success or an error code
(WRONG_APPLICATION, STATUS_ERROR, INVALID_APDU, INVALID_ARGUMENT).

**called by** `woz_aliro_parse_select_response`  ·  **calls** `aid_for_phase`, `woz_aliro_parse_proprietary_information`
