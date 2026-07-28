<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_assert.h`

*No module docstring. First commit: "aliro: presence-assertion protocol (HMAC-signed range statement)".*

**used by** [`modules/woz_aliro/include/aliro_assert_ec.h`](aliro_assert_ec.h.md), [`modules/woz_aliro/src/aliro_assert.c`](../modules.woz_aliro.src/aliro_assert.c.md)

## API

### `struct aliro_assert`
`modules/woz_aliro/include/aliro_assert.h:115`

The assertion fields. Wire layout (big-endian multi-byte), the tag covers
every byte before it:
magic(2)=A1 50 | version(1)=03 | alg(1) | status(1) | nonce(16) |
cred_id(8) | distance_cm(2) | range_flags(1) | sts_quality(2) |
trust_level(1) | uptime_ms(8) | unix_ms(8) | sig(64)
= 115 bytes.
The three integrity fields sit next to distance_cm because they qualify it:
reading the distance without them is reading a number with its provenance
stripped off.
