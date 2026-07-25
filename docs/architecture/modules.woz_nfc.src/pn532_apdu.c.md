<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/pn532_apdu.c`

@file pn532_apdu.c
PN532 APDU command planner: parse ISO 7816-4 APDU structure (Case 1-4, short/extended), emit
passthrough or fragmented transport frames, handle GetResponse for extended data retrieval.

**depends on** [`modules/woz_nfc/src/pn532_apdu.h`](pn532_apdu.h.md)

## API

### `struct parsed_apdu`
`modules/woz_nfc/src/pn532_apdu.c:14`

Parsed APDU command: data_offset/data_length (position in input, size in bytes), le (response max
size, 256/65536 if encoded as 0), extended (long form), has_data/has_le (flags).

### `static bool parse_apdu(const uint8_t *input, size_t length, struct parsed_apdu *parsed)`
`modules/woz_nfc/src/pn532_apdu.c:28`

Parse APDU structure (ISO 7816-4 Case 1-4, short/extended form): identifies CLA/INS/P1/P2,
optional Lc (data length) and data, optional Le (response length). Returns true on valid syntax,
false otherwise.

**called by** `woz_pn532_apdu_plan_init`

### `int woz_pn532_apdu_plan_init(const uint8_t *input, size_t input_length, struct woz_pn532_apdu_plan *plan)`
`modules/woz_nfc/src/pn532_apdu.c:88`

Parse an APDU and prepare its transport representation. Unsupported or
malformed APDUs are deliberately passed through so the peer remains the
authority that returns an ISO 7816 status word.

**calls** `parse_apdu`

### `static int emit_passthrough(struct woz_pn532_apdu_plan *plan, uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_nfc/src/pn532_apdu.c:127`

Emit a passthrough (raw APDU command): copy input as-is. Returns 0 on success, -2 if buffer too
small or already emitted.

**called by** `woz_pn532_apdu_plan_next`

### `static int emit_get_response(struct woz_pn532_apdu_plan *plan, uint8_t *output, size_t output_capacity, size_t *output_length)`
`modules/woz_nfc/src/pn532_apdu.c:143`

Emit a GetResponse command (00 C0 00 00 [Le]): copy input, adjust Le for extended if needed.
Returns 0 on success, -2 if buffer too small or already emitted.

**called by** `woz_pn532_apdu_plan_next`

### `static int emit_envelope(struct woz_pn532_apdu_plan *plan, uint8_t *output, size_t output_capacity, size_t *output_length, bool *more_internal)`
`modules/woz_nfc/src/pn532_apdu.c:168`

Emit one chunk of an APDU command: fragments data if needed (WOZ_PN532_ENVELOPE_DATA_MAX per
frame), sets more bit if more chunks follow, appends Le if this is the last chunk. Returns 0 on
success, -2 if buffer too small or data exhausted.

**called by** `woz_pn532_apdu_plan_next`

### `int woz_pn532_apdu_plan_next(struct woz_pn532_apdu_plan *plan, uint8_t *output, size_t output_capacity, size_t *output_length, bool *more_internal)`
`modules/woz_nfc/src/pn532_apdu.c:219`

Emit the next wire APDU. more_internal is true only when the transport must
consume a 9000 response and send another fragment before notifying Aliro.

**calls** `emit_envelope`, `emit_get_response`, `emit_passthrough`
