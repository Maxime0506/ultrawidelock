<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_advtag.c`

Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1), shared by the
BLE transport (live advertising) and the host KAT suite (spec section 20 worked examples).

**depends on** [`modules/woz_aliro/include/aliro_advtag.h`](../modules.woz_aliro.include/aliro_advtag.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md)  ·  **discussed in** [`docs/protocol-notes.md`](../../protocol-notes.md)

## API

### `int aliro_advtag_derive(const uint8_t grk[16], const uint8_t adva_msb[6], uint32_t expiry_unix, uint8_t tag[ALIRO_ADVTAG_LEN])`
`modules/woz_aliro/src/aliro_advtag.c:22`

Derive a 4-byte advertisement tag from a 16-byte global reader key, 6-byte BLE address MSB, and
32-bit UNIX expiry; tag allows a peer to verify freshness without decryption.
