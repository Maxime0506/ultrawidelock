<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_assert_ec.h`

*No module docstring. First commit: "assert: bind the P-256 seam to aliro_prim".*

**depends on** [`modules/woz_aliro/include/aliro_assert.h`](aliro_assert.h.md), [`modules/woz_aliro/include/aliro_prim.h`](aliro_prim.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_assert_ec.c`](../modules.woz_aliro.src/aliro_assert_ec.c.md)

## API

### `struct aliro_assert_ec_priv`
`modules/woz_aliro/include/aliro_assert_ec.h:30`

Key material the two binders expect as their ctx. Pass a pointer to one of
these as the void *ctx argument of aliro_assert_build_p256 / _verify_p256.

### `struct aliro_assert_ec_pub`
`modules/woz_aliro/include/aliro_assert_ec.h:37`

Uncompressed ECDSA-P256 public key point: 0x04 || X || Y (65 bytes).
