<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/tlv.h`

Minimal strict BER/DER-TLV reader for Aliro APDU payloads.

**used by** [`modules/woz_aliro_stack/src/protocol/ble_message.c`](ble_message.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](nfc_auth.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](nfc_select.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](nfc_step_up.c.md), [`modules/woz_aliro_stack/src/protocol/tlv.c`](tlv.c.md)

## API

### `struct woz_aliro_tlv`
`modules/woz_aliro_stack/src/protocol/tlv.h:15`

Parsed TLV (Tag-Length-Value): tag (uint32), value (pointer to payload bytes), length (payload
size), encoded_length (total tag+length+value bytes).
