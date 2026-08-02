<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_crypto.c`

@file matter_crypto.c — AES-128-CCM, the Matter nonce, and the key schedule.

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_crypto.h`](../modules.woz_matter.include/matter_crypto.h.md)

## API

### `struct cbc_mac`
`modules/woz_matter/src/matter_crypto.c:44`

Streaming CBC-MAC state: no buffer proportional to the message.

### `static int mac_flush(struct cbc_mac *m)`
`modules/woz_matter/src/matter_crypto.c:85`

Flush a partial block, zero-padded, as CCM requires at each section end.

**called by** `ccm_mac`  ·  **calls** `mac_block`

### `static void ctr_block(const uint8_t nonce[MATTER_NONCE_LEN], uint16_t i, uint8_t out[AES_BLOCK])`
`modules/woz_matter/src/matter_crypto.c:102`

Counter block A_i, per RFC 3610: flags | nonce | i, with only L-1 in the
flags because A blocks carry no Adata or tag-length fields.

**called by** `ccm_ctr`, `ccm_tag`

### `static int ccm_mac(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t t_out[AES_BLOCK])`
`modules/woz_matter/src/matter_crypto.c:111`

Run the CBC-MAC over B0, the length-prefixed AAD, and the payload.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `mac_block`, `mac_flush`, `mac_update`

### `static int ccm_ctr(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *in, size_t len, uint8_t *out)`
`modules/woz_matter/src/matter_crypto.c:166`

XOR the CTR keystream over @p len bytes, starting at counter block 1.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `ctr_block`

### `static int ccm_tag(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t t[AES_BLOCK], uint8_t tag_out[MATTER_TAG_LEN])`
`modules/woz_matter/src/matter_crypto.c:196`

Mask the raw CBC-MAC with S0 to produce the transmitted tag.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `ctr_block`

### `static bool tag_equal(const uint8_t *a, const uint8_t *b, size_t len)`
`modules/woz_matter/src/matter_crypto.c:215`

Constant time: a tag comparison must not leak how far it matched.

**called by** `matter_aead_decrypt`

<details><summary>Undocumented (8)</summary>

- `mac_block`
- `mac_update`
- `matter_build_nonce` — tested: matter crypto
- `matter_derive_session_keys` — tested: matter crypto
- `matter_aead_encrypt` — tested: matter crypto
- `matter_aead_decrypt` — tested: matter crypto
- `matter_crypto_seal` — tested: matter crypto; matter exchange
- `matter_crypto_open` — tested: matter crypto; matter exchange

</details>
