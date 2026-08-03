<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_crypto.c`

@file matter_crypto.c — AES-128-CCM, the Matter nonce, and the key schedule.

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_crypto.h`](../modules.woz_matter.include/matter_crypto.h.md)

## API

### `struct cbc_mac`
`modules/woz_matter/src/matter_crypto.c:44`

Streaming CBC-MAC state: no buffer proportional to the message.

### `static int mac_block(struct cbc_mac *m, const uint8_t b[AES_BLOCK])`
`modules/woz_matter/src/matter_crypto.c:54`

XOR one 128-bit block into the CBC-MAC state and encrypt it with AES-ECB.

**called by** `ccm_mac`, `mac_flush`, `mac_update`

### `static int mac_update(struct cbc_mac *m, const uint8_t *p, size_t len)`
`modules/woz_matter/src/matter_crypto.c:66`

Update CBC-MAC state with input bytes: accumulate into partial blocks and process full 128-bit
blocks through the cipher. Returns 0 on success or cipher error.

**called by** `ccm_mac`  ·  **calls** `mac_block`

### `static int mac_flush(struct cbc_mac *m)`
`modules/woz_matter/src/matter_crypto.c:92`

Flush a partial block, zero-padded, as CCM requires at each section end.

**called by** `ccm_mac`  ·  **calls** `mac_block`

### `static void ctr_block(const uint8_t nonce[MATTER_NONCE_LEN], uint16_t i, uint8_t out[AES_BLOCK])`
`modules/woz_matter/src/matter_crypto.c:109`

Counter block A_i, per RFC 3610: flags | nonce | i, with only L-1 in the
flags because A blocks carry no Adata or tag-length fields.

**called by** `ccm_ctr`, `ccm_tag`

### `static int ccm_mac(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t t_out[AES_BLOCK])`
`modules/woz_matter/src/matter_crypto.c:118`

Run the CBC-MAC over B0, the length-prefixed AAD, and the payload.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `mac_block`, `mac_flush`, `mac_update`

### `static int ccm_ctr(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *in, size_t len, uint8_t *out)`
`modules/woz_matter/src/matter_crypto.c:173`

XOR the CTR keystream over @p len bytes, starting at counter block 1.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `ctr_block`

### `static int ccm_tag(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t t[AES_BLOCK], uint8_t tag_out[MATTER_TAG_LEN])`
`modules/woz_matter/src/matter_crypto.c:203`

Mask the raw CBC-MAC with S0 to produce the transmitted tag.

**called by** `matter_aead_decrypt`, `matter_aead_encrypt`  ·  **calls** `ctr_block`

### `static bool tag_equal(const uint8_t *a, const uint8_t *b, size_t len)`
`modules/woz_matter/src/matter_crypto.c:222`

Constant time: a tag comparison must not leak how far it matched.

**called by** `matter_aead_decrypt`

### `int matter_build_nonce(uint8_t security_flags, uint32_t message_counter, uint64_t node_id, uint8_t out[MATTER_NONCE_LEN])`
`modules/woz_matter/src/matter_crypto.c:236`

Build an AES-CCM nonce from security flags, message counter, and node ID in little-endian form;
returns MATTER_OK on success.

**called by** `matter_crypto_open`, `matter_crypto_seal`

### `int matter_derive_session_keys(const uint8_t *secret, size_t secret_len, const uint8_t *salt, size_t salt_len, bool resume, struct matter_session_keys *out)`
`modules/woz_matter/src/matter_crypto.c:259`

Derive session keys from a shared secret using HKDF for Matter secure channel setup.
Expands secret into i2r, r2i, and attestation_challenge keys using either normal or resume
derivation context.
Returns MATTER_E_INVAL if secret, out are NULL or secret_len is zero; returns MATTER_E_INVAL if
salt_len is nonzero but salt is NULL; returns MATTER_E_STATE if HKDF fails.

### `int matter_aead_encrypt(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct_out, uint8_t tag_out[MATTER_TAG_LEN])`
`modules/woz_matter/src/matter_crypto.c:291`

Encrypt a plaintext with AES-CCM, optionally authenticated with AAD, by computing the CBC-MAC,
generating the authentication tag, and encrypting the plaintext with CTR; returns MATTER_OK on
success.

**called by** `matter_crypto_seal`  ·  **calls** `ccm_ctr`, `ccm_mac`, `ccm_tag`

### `int matter_aead_decrypt(const uint8_t key[MATTER_KEY_LEN], const uint8_t nonce[MATTER_NONCE_LEN], const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[MATTER_TAG_LEN], uint8_t *pt_out)`
`modules/woz_matter/src/matter_crypto.c:329`

Decrypt an AES-CCM ciphertext with an authentication tag and optional AAD, verifying the tag in
constant time before returning plaintext; returns MATTER_OK on success or MATTER_E_TYPE if the
tag does not verify.

**called by** `matter_crypto_open`  ·  **calls** `ccm_ctr`, `ccm_mac`, `ccm_tag`, `tag_equal`

### `int matter_crypto_seal(const struct matter_msg_header *h, const uint8_t key[MATTER_KEY_LEN], uint64_t sender_node_id, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_crypto.c:393`

Encrypt and authenticate a Matter message: encode header into output buffer, build nonce from
security flags and counter, and encrypt payload with AAD set to the encoded header bytes. Caller
must ensure output capacity >= header length + payload length + MATTER_TAG_LEN. Returns MATTER_OK
on success or encoding error.

**calls** `matter_aead_encrypt`, `matter_build_nonce`

### `int matter_crypto_open(const uint8_t *buf, size_t len, const uint8_t key[MATTER_KEY_LEN], uint64_t sender_node_id, struct matter_msg_header *h, uint8_t *pt_out, size_t pt_cap, size_t *pt_len)`
`modules/woz_matter/src/matter_crypto.c:440`

Decrypt and verify a Matter message: decode header, extract ciphertext and authentication tag,
build nonce from security flags and counter, and decrypt with AAD set to the message header.
Returns MATTER_OK on successful decryption, MATTER_E_INVAL on bad parameters, MATTER_E_TRUNC if
ciphertext too short for tag, MATTER_E_NOSPACE if plaintext exceeds output capacity.

**calls** `matter_aead_decrypt`, `matter_build_nonce`
