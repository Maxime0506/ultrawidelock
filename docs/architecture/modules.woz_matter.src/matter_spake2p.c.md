<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_spake2p.c`

@file matter_spake2p.c — PBKDF2, the SPAKE2+ transcript and confirmations.

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_spake2p.h`](../modules.woz_matter.include/matter_spake2p.h.md)

## API

### `int matter_pbkdf2_sha256(const uint8_t *password, size_t password_len, const uint8_t *salt, size_t salt_len, uint32_t iterations, uint8_t *out, size_t out_len)`
`modules/woz_matter/src/matter_spake2p.c:49`

Derive an output key using PBKDF2-SHA256 with the given password, salt, and iteration count;
returns MATTER_E_INVAL if parameters are invalid or MATTER_E_NOSPACE if the salt is too long.

**called by** `matter_spake2p_w0w1`

### `int matter_spake2p_w0w1(uint32_t passcode, const uint8_t *salt, size_t salt_len, uint32_t iterations, uint8_t w0[MATTER_SPAKE_SCALAR_LEN], uint8_t w1[MATTER_SPAKE_SCALAR_LEN])`
`modules/woz_matter/src/matter_spake2p.c:113`

Derive SPAKE2+ w0 and w1 scalars from a 32-bit passcode using PBKDF2-SHA256 with salt and
iterations. Passcode is encoded little-endian; each result is reduced modulo the P-256 order
using 40 bytes of derived material (8 bytes extra for negligible bias). Returns MATTER_OK on
success.

**calls** `matter_pbkdf2_sha256`

### `int matter_spake2p_context(const uint8_t *req, size_t req_len, const uint8_t *resp, size_t resp_len, uint8_t out[MATTER_SPAKE_HASH_LEN])`
`modules/woz_matter/src/matter_spake2p.c:154`

Hash the SPAKE2+ context from optional request and response payloads; context is always hashed
but payloads are only included if present.

### `static void tt_put(uint8_t *out, size_t *off, const uint8_t *data, size_t len)`
`modules/woz_matter/src/matter_spake2p.c:180`

Append one length-prefixed transcript element.

**called by** `matter_spake2p_transcript`

### `int matter_spake2p_transcript(const uint8_t context[MATTER_SPAKE_HASH_LEN], const uint8_t pa[MATTER_SPAKE_POINT_LEN], const uint8_t pb[MATTER_SPAKE_POINT_LEN], const uint8_t z[MATTER_SPAKE_POINT_LEN], const uint8_t v[MATTER_SPAKE_POINT_LEN], const uint8_t w0[MATTER_SPAKE_SCALAR_LEN], uint8_t *out, size_t *out_len)`
`modules/woz_matter/src/matter_spake2p.c:201`

Assemble the SPAKE2+ transcript from context, identity empty strings, M, N, exchange points,
shared secret Z, ephemeral V, and w0 scalar; returns MATTER_E_NOSPACE if output buffer is too
small.

**calls** `tt_put`

### `int matter_spake2p_p2(const uint8_t *tt, size_t tt_len, const uint8_t pa[MATTER_SPAKE_POINT_LEN], const uint8_t pb[MATTER_SPAKE_POINT_LEN], struct matter_spake2p_result *out)`
`modules/woz_matter/src/matter_spake2p.c:243`

Derive confirmation and session keys from the SPAKE2+ transcript and exchange points; swaps Ka
and Ke derivation order and produces confirmation codes Ca over peer's point and Cb over own
point.

### `bool matter_spake2p_verify(const uint8_t expected[MATTER_SPAKE_HASH_LEN], const uint8_t got[MATTER_SPAKE_HASH_LEN])`
`modules/woz_matter/src/matter_spake2p.c:279`

Compare two SPAKE2+ hash values in constant time; returns true if they match exactly.
