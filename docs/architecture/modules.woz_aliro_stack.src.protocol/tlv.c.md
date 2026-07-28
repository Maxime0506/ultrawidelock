<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/tlv.c`

@file tlv.c
BER-TLV parser and encoder for Aliro protocol: parse TLVs with definite length and advance
offset, compute encoded sizes, and write new TLVs.

**depends on** [`modules/woz_aliro_stack/src/protocol/tlv.h`](tlv.h.md)

## API

### `int woz_aliro_tlv_next(const uint8_t *data, size_t data_length, size_t *offset, struct woz_aliro_tlv *out)`
`modules/woz_aliro_stack/src/protocol/tlv.c:12`

Parse one TLV at *offset and advance the offset past it. Indefinite and
non-minimal DER lengths are rejected.

### `static size_t tag_size(uint32_t tag)`
`modules/woz_aliro_stack/src/protocol/tlv.c:82`

Compute the BER-TLV tag byte count for a given tag value: 1 byte for 0x00–0xff, 2 for
0x0100–0xffff, 3 for 0x010000–0xffffff, or 0 if tag is unsupported.

**called by** `woz_aliro_tlv_encoded_size`, `woz_aliro_tlv_write`

### `static size_t length_size(size_t length)`
`modules/woz_aliro_stack/src/protocol/tlv.c:100`

Compute the BER-TLV length field byte count for a given value length: 1 byte for 0x00–0x7f, 2 for
0x80–0xff, 3 for 0x0100–0xffff, or 0 if length is unsupported.

**called by** `woz_aliro_tlv_encoded_size`

### `size_t woz_aliro_tlv_encoded_size(uint32_t tag, size_t value_length)`
`modules/woz_aliro_stack/src/protocol/tlv.c:114`

Return the encoded size of a definite-length TLV, or zero when the tag or
length cannot be represented by this codec. Tags are supplied in their
normal big-endian encoded form (for example 0x7f66).

**called by** `woz_aliro_tlv_write`  ·  **calls** `length_size`, `tag_size`

### `int woz_aliro_tlv_write(uint8_t *data, size_t data_capacity, size_t *offset, uint32_t tag, const uint8_t *value, size_t value_length)`
`modules/woz_aliro_stack/src/protocol/tlv.c:125`

Append one definite-length TLV at *offset.

**calls** `tag_size`, `woz_aliro_tlv_encoded_size`
