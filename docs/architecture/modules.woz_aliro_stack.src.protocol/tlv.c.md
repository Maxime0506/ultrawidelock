<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/tlv.c`

**depends on** [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `int woz_aliro_tlv_next(const uint8_t *data, size_t data_length, size_t *offset, struct woz_aliro_tlv *out)`
`modules/woz_aliro_stack/src/protocol/tlv.c:7`

Parse one TLV at *offset and advance the offset past it. Indefinite and
non-minimal DER lengths are rejected.

### `size_t woz_aliro_tlv_encoded_size(uint32_t tag, size_t value_length)`
`modules/woz_aliro_stack/src/protocol/tlv.c:101`

Return the encoded size of a definite-length TLV, or zero when the tag or
length cannot be represented by this codec. Tags are supplied in their
normal big-endian encoded form (for example 0x7f66).

**called by** `woz_aliro_tlv_write`  ·  **calls** `length_size`, `tag_size`

### `int woz_aliro_tlv_write(uint8_t *data, size_t data_capacity, size_t *offset, uint32_t tag, const uint8_t *value, size_t value_length)`
`modules/woz_aliro_stack/src/protocol/tlv.c:112`

Append one definite-length TLV at *offset.

**calls** `tag_size`, `woz_aliro_tlv_encoded_size`

<details><summary>Undocumented (2)</summary>

- `tag_size`
- `length_size`

</details>
