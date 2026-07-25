<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/nfc_auth.h`

Aliro 1.0 expedited authentication APDU codecs.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](nfc_auth.c.md), [`modules/woz_aliro_stack/src/session.cpp`](../modules.woz_aliro_stack.src/session.cpp.md)

## API

### `struct woz_aliro_auth0_command`
`modules/woz_aliro_stack/src/protocol/nfc_auth.h:32`

Parsed NFC AUTH0 command: authentication policy and protocol version from the reader, reader
ephemeral public key, transaction ID, reader ID, and optional vendor extension.

### `struct woz_aliro_auth0_response`
`modules/woz_aliro_stack/src/protocol/nfc_auth.h:47`

Parsed NFC AUTH0 response: credential ephemeral public key (65 bytes), cryptogram (variable
length), and optional vendor extension.

### `struct woz_aliro_auth1_response`
`modules/woz_aliro_stack/src/protocol/nfc_auth.h:60`

Parsed NFC AUTH1 response: credential public key (65 bytes), signature (variable length),
signaling bitmap, and two signed timestamps (credential and revocation, each 20 bytes if
present).
