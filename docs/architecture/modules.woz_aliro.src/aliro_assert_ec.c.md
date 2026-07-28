<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_assert_ec.c`

Binds the aliro_assert P-256 seam to aliro_prim's ECDSA (see aliro_assert_ec.h).
The only file in the presence path with a crypto-backend dependency, which is
exactly why it is separate: aliro_assert.c keeps its cbmc and fuzz harnesses.

**depends on** [`modules/woz_aliro/include/aliro_assert_ec.h`](../modules.woz_aliro.include/aliro_assert_ec.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md)

<details><summary>Undocumented (2)</summary>

- `aliro_assert_ec_sign`
- `aliro_assert_ec_verify`

</details>
