<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/piv_ccid/piv_apdu.c`

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](../ports.esp32.components.piv_ccid.include/piv_apdu.h.md)

## API

### `struct command_apdu`
`ports/esp32/components/piv_ccid/piv_apdu.c:71`

A parsed ISO 7816 command APDU: class, instruction, parameters, data payload, and optional Le
(expected response length).

### `static int finish(const uint8_t *data, size_t data_len, uint16_t sw, uint8_t *response, size_t response_cap, size_t *response_len)`
`ports/esp32/components/piv_ccid/piv_apdu.c:86`

Write data and a status word to the response buffer and record its length, returning 0 on success
or -1 if the buffer is too small.

**called by** `handle_change_pin`, `handle_general_authenticate`, `handle_get_data`, `handle_verify`, `piv_apdu_transmit`

### `static int parse_command(const uint8_t *command, size_t command_len, struct command_apdu *apdu)`
`ports/esp32/components/piv_ccid/piv_apdu.c:106`

Parse an ISO 7816 command APDU from bytes: extract CLA, INS, P1, P2, data length, data, and
optional Le. Return 0 on success or -1 if the length is invalid.

**called by** `piv_apdu_transmit`

### `static size_t ber_length_bytes(size_t len)`
`ports/esp32/components/piv_ccid/piv_apdu.c:145`

Return the number of bytes needed to encode a BER length: 1 if less than 128, 2 if up to 255, or
3 if up to 65535.

**called by** `build_certificate`, `wrap_tlv`

### `static int put_ber_length(uint8_t *out, size_t cap, size_t len, size_t *written)`
`ports/esp32/components/piv_ccid/piv_apdu.c:154`

Encode a BER length into the output buffer: 1 byte for lengths under 128, 2 for up to 255, 3 for
up to 65535. Return 0 on success or -1 if the buffer is too small.

**called by** `build_certificate`, `wrap_tlv`

### `static int emit_pending(struct piv_apdu *piv, size_t requested, uint8_t *response, size_t response_cap, size_t *response_len)`
`ports/esp32/components/piv_ccid/piv_apdu.c:211`

Emit a chunk of a pending response, up to the requested size, followed by a status word
indicating how many bytes remain: SW_SUCCESS if done, or SW_BYTES_REMAINING with the count.

**called by** `piv_apdu_transmit`, `send_data`

### `static bool aid_matches(const uint8_t *data, size_t len)`
`ports/esp32/components/piv_ccid/piv_apdu.c:264`

Return true if the data buffer matches the PIV AID or the PIV AID truncated form.

**called by** `piv_apdu_transmit`

### `static uint16_t pin_result_status(int result, uint8_t retries)`
`ports/esp32/components/piv_ccid/piv_apdu.c:419`

Return the status word for a PIN verify or change result: 0x9000 for success, 0x63Cn for retries
remaining, 0x6983 if blocked, or 0x6982 otherwise.

**called by** `handle_change_pin`, `handle_verify`

### `static size_t der_integer(const uint8_t value[32], uint8_t out[35])`
`ports/esp32/components/piv_ccid/piv_apdu.c:512`

Encode a 32-byte value as a DER integer with a leading zero byte if the high bit is set,
returning the encoded length.

**called by** `handle_general_authenticate`

### `void piv_apdu_init(struct piv_apdu *piv, const struct piv_apdu_backend *backend, void *backend_ctx, bool pin_required)`
`ports/esp32/components/piv_ccid/piv_apdu.c:627`

Initialize a PIV APDU engine with a backend, backend context, and a flag indicating whether PIN
is required for presence signatures.

### `void piv_apdu_reset(struct piv_apdu *piv)`
`ports/esp32/components/piv_ccid/piv_apdu.c:643`

Reset the PIV APDU engine state: clear selection, PIN verification, and pending data.

<details><summary>Undocumented (10)</summary>

- `wrap_tlv`
- `send_data`
- `build_ccc`
- `build_chuid`
- `build_certificate`
- `handle_get_data`
- `handle_verify`
- `handle_change_pin`
- `handle_general_authenticate`
- `piv_apdu_transmit` — tested: objects and chaining; pin and presence signature boundary; pinless uwb policy

</details>
