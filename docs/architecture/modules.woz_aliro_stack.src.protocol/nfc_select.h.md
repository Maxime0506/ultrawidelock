<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/nfc_select.h`

@file nfc_select.h
Parsed result of an NFC SELECT command for the Aliro applet: negotiated protocol version, maximum
command and response data lengths (from TLV or default), extended-length support, and the raw
proprietary information TLV (A5 tag) for further parsing.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](nfc_select.c.md), [`modules/woz_aliro_stack/src/session.cpp`](../modules.woz_aliro_stack.src/session.cpp.md)

## API

### `struct woz_aliro_select_response`
`modules/woz_aliro_stack/src/protocol/nfc_select.h:41`

Parsed NFC SELECT response: negotiated protocol version, max command/response data lengths (from
7F66 TLV or defaults), extended-length support flag, and the complete proprietary information TLV
(A5 tag). The TLV view is valid only as long as the input response buffer is valid.
