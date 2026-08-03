<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_assert_ec.c`

Binds the aliro_assert P-256 seam to aliro_prim's ECDSA (see aliro_assert_ec.h).
The only file in the presence path with a crypto-backend dependency, which is
exactly why it is separate: aliro_assert.c keeps its cbmc and fuzz harnesses.

**depends on** [`modules/woz_aliro/include/aliro_assert_ec.h`](../modules.woz_aliro.include/aliro_assert_ec.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md)

## API

### `int aliro_assert_ec_sign(void *ctx, const uint8_t *msg, size_t msg_len, uint8_t sig[ALIRO_ASSERT_SIG_LEN])`
`modules/woz_aliro/src/aliro_assert_ec.c:21`

ECDSA-P256-SHA256 sign: hash msg internally and return 64-byte signature, or -1 on error.

### `int aliro_assert_ec_verify(void *ctx, const uint8_t *msg, size_t msg_len, const uint8_t sig[ALIRO_ASSERT_SIG_LEN])`
`modules/woz_aliro/src/aliro_assert_ec.c:39`

ECDSA-P256-SHA256 verify: return 0 if sig is valid over msg with the stored public point, -1 if
invalid or on error.
