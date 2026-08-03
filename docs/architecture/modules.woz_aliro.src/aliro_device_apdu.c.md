<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_device_apdu.c`

Implementation of the device-side Access-Protocol wire codec declared in
aliro_device_apdu.h: ISO7816 case-4 unwrapping, status-word appending, parsers
for the reader's AUTH0, AUTH1 and EXCHANGE command TLVs, and builders for the
three device responses. Every function is bounds-checked byte manipulation over
caller-owned buffers with no allocation, so it round-trips against the reader's
own builders and parsers in aliro_apdu.c under the host tests.

**depends on** [`modules/woz_aliro/include/aliro_device_apdu.h`](../modules.woz_aliro.include/aliro_device_apdu.h.md)

## API

### `int aliro_apdu_unwrap(const uint8_t *apdu, size_t len, uint8_t *ins, const uint8_t **data, size_t *data_len)`
`modules/woz_aliro/src/aliro_device_apdu.c:21`

Extract the instruction byte and data payload from a 5-byte ISO 7816 case-4 short-form APDU with
CLA 0x80, returning 0 on success or -1 if the APDU is malformed.

### `int aliro_apdu_append_sw(uint8_t *buf, size_t *len, size_t cap, uint16_t sw)`
`modules/woz_aliro/src/aliro_device_apdu.c:45`

Append a 2-byte big-endian status code to the output buffer and increment its length, returning 0
on success or -1 if the buffer is full.

### `int aliro_dev_parse_auth0_cmd(const uint8_t *tlv, size_t len, struct aliro_auth0_command *c)`
`modules/woz_aliro/src/aliro_device_apdu.c:60`

Parse the TLV fields of an AUTH0 command into the struct: extract and validate the phase, user
policy, version, reader ephemeral public key, transaction ID, and reader ID.

### `int aliro_dev_parse_auth1_cmd(const uint8_t *tlv, size_t len, struct aliro_auth1_command *c)`
`modules/woz_aliro/src/aliro_device_apdu.c:98`

Parse the TLV fields of an AUTH1 command into the struct: extract the credential type if present
and the mandatory 64-byte reader signature.

### `int aliro_dev_parse_exchange_cmd(const uint8_t *plain, size_t len, struct aliro_exchange_command *c)`
`modules/woz_aliro/src/aliro_device_apdu.c:118`

Parse optional TLV fields from an EXCHANGE command plaintext: extract reader status and
URSK-ready flag if present, leaving unset fields at their default values.

### `int aliro_dev_build_auth0_resp(const uint8_t device_eph_pub[65], const uint8_t *cryptogram64, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_aliro/src/aliro_device_apdu.c:138`

Build a TLV-encoded AUTH0 response containing the device ephemeral public key and optionally a
64-byte cryptogram.

### `int aliro_dev_build_auth1_resp(const uint8_t device_sig[64], const uint8_t *device_pub65, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_aliro/src/aliro_device_apdu.c:155`

Build a TLV-encoded AUTH1 response containing the device signature and optionally the device
public key.

### `int aliro_dev_build_exchange_resp(uint16_t error, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_aliro/src/aliro_device_apdu.c:172`

Build a 4-byte EXCHANGE response body with length prefix 0x0002 and the given error code in
big-endian format.
