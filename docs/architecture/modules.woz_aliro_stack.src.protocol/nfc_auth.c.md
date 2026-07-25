<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/nfc_auth.c`

@file nfc_auth.c
NFC Aliro protocol command builders: AUTH0 and AUTH1 APDU encoding, authentication data
construction, and response parsing for credential exchange and signature verification over NFC.

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](nfc_auth.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `static int put_tlv(uint8_t *output, size_t capacity, size_t *offset, uint32_t tag, const uint8_t *value, size_t length)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:17`

Write a TLV (Tag-Length-Value) field to output buffer at *offset; return WOZ_ALIRO_AUTH_OK on
success or WOZ_ALIRO_AUTH_BUFFER_TOO_SMALL if capacity is exceeded.

**called by** `woz_aliro_build_auth0_command`, `woz_aliro_build_auth1_command_ex`, `woz_aliro_build_authentication_data`

### `int woz_aliro_build_auth0_command(const struct woz_aliro_auth0_command *params, uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:30`

Build an NFC AUTH0 command APDU with reader ephemeral public key, transaction and reader
identifiers, optional vendor extension; return WOZ_ALIRO_AUTH_OK on success or error code if
arguments are invalid or output capacity is exceeded.

**calls** `put_tlv`

### `int woz_aliro_parse_auth0_response(const uint8_t *response, size_t response_length, int fast_requested, struct woz_aliro_auth0_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:83`

Parse an AUTH0 response APDU (status 0x9000, TLV-encoded) and extract credential ephemeral public
key (tag 0x86), optional cryptogram (tag 0x9d if fast mode requested), and optional vendor
extension (tag 0xb2); return WOZ_ALIRO_AUTH_OK on success or error code on malformed input or
status error.

### `int woz_aliro_build_authentication_data(const uint8_t reader_identifier[WOZ_ALIRO_READER_ID_SIZE], const uint8_t credential_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE], const uint8_t reader_ephemeral_public_key[WOZ_ALIRO_PUBLIC_KEY_SIZE], const uint8_t transaction_identifier[WOZ_ALIRO_TRANSACTION_ID_SIZE], uint32_t usage, uint8_t output[WOZ_ALIRO_AUTH_DATA_SIZE])`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:141`

Build the fixed-size 256-byte Aliro authentication data structure containing TLV-encoded reader
identifier, credential and reader ephemeral public keys (coordinate pairs only), transaction
identifier, and usage bitmap; return WOZ_ALIRO_AUTH_OK on success or error code if any input is
invalid.

**calls** `put_tlv`

### `int woz_aliro_build_auth1_command(uint8_t command_parameters, const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE], uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:178`

Build an NFC AUTH1 command APDU with ECDSA signature and no reader certificate; return
WOZ_ALIRO_AUTH_OK on success or error code if arguments are invalid or output capacity is
exceeded.

**calls** `woz_aliro_build_auth1_command_ex`

### `int woz_aliro_build_auth1_command_ex(uint8_t command_parameters, const uint8_t signature[WOZ_ALIRO_SIGNATURE_SIZE], const uint8_t *reader_certificate, size_t reader_certificate_length, uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:191`

Build an NFC AUTH1 command APDU with ECDSA signature and optional reader certificate; return
WOZ_ALIRO_AUTH_OK on success or error code if arguments are invalid or output capacity is
exceeded.

**called by** `woz_aliro_build_auth1_command`  ·  **calls** `put_tlv`

### `int woz_aliro_parse_auth1_plaintext(const uint8_t *plaintext, size_t plaintext_length, int public_key_requested, struct woz_aliro_auth1_response *result)`
`modules/woz_aliro_stack/src/protocol/nfc_auth.c:234`

Parse plaintext AUTH1 response (TLV-encoded): credential public key (tag 0x5a if
public_key_requested), ECDSA signature (tag 0x9e), signaling bitmap (tag 0x5e), and optional
signed timestamps (tags 0x91, 0x92); return WOZ_ALIRO_AUTH_OK on success or error code if format
is invalid or required fields are missing.
