<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_kdf.c`

@file ccc_kdf.c — UWB key schedule + SP0 Pre-POLL frame codec.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](ccc_kdf.h.md)

## API

### `static void put_be32(uint8_t *out, uint32_t v)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:20`

@brief Store v big-endian into out (4 bytes).
@param out Destination buffer for the 4 big-endian bytes.
@param v Value to store.

**called by** `ccc_derive_sts_v`, `ccc_derive_uad`, `kdf108_block`, `sp0_nonce`

### `static uint32_t get_be32(const uint8_t *in)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:33`

@brief Load a big-endian uint32 from in (4 bytes).
@param in Source buffer holding 4 big-endian bytes.
@return The decoded uint32 value.

**called by** `ccc_derive_sts_v`

### `static void xor_bytes(uint8_t *out, const uint8_t *a, const uint8_t *b, size_t n)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:46`

@brief XOR n bytes: out = a ^ b (out may alias a).
@param out Destination buffer for the XOR result.
@param a First input buffer.
@param b Second input buffer.
@param n Number of bytes to XOR.

**called by** `ccc_aes_cmac`, `ccc_sp0_decrypt`, `ccc_sp0_encrypt`, `sp0_cbc_mac`, `sp0_ctr`

### `static uint8_t block_lshift1(const uint8_t in[AES_BLOCK_LEN], uint8_t out[AES_BLOCK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:61`

@brief One-bit left shift of a 128-bit big-endian block.
@param in Input 128-bit block.
@param out Output buffer receiving the shifted block.
@return The bit shifted out of the most significant bit position.

**called by** `cmac_subkey`

### `static void cmac_subkey(const uint8_t l[AES_BLOCK_LEN], uint8_t out[AES_BLOCK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:77`

@brief CMAC subkey derivation: K = (L << 1), XOR Rb if the top bit was set.
@param l Input 128-bit value (L, or a previously derived subkey).
@param out Output buffer receiving the derived subkey.

**called by** `ccc_aes_cmac`  ·  **calls** `block_lshift1`

### `int ccc_aes_cmac(const uint8_t *key, size_t key_bits, const uint8_t *msg, size_t msg_len, uint8_t tag[CCC_CMAC_TAG_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:88`

@brief AES-CMAC authentication tag over message.

**called by** `ccc_derive_salted_hash`, `kdf108_block`  ·  **calls** `cmac_subkey`, `xor_bytes`

### `static int kdf108_block(const uint8_t *kdk, size_t kdk_bits, uint32_t counter, const uint8_t *label, size_t label_len, const uint8_t *context, size_t ctx_len, uint32_t l_bits, uint8_t out[AES_BLOCK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:163`

@brief One counter-mode CMAC block: CMAC(kdk, counter || label || 0x00 || context || l_bits).
@param kdk Key-derivation key.
@param kdk_bits kdk size in bits.
@param counter Block counter value.
@param label KDF label bytes.
@param label_len Length of label in bytes.
@param context KDF context bytes (may be omitted if ctx_len is 0).
@param ctx_len Length of context in bytes.
@param l_bits Requested output length in bits, encoded into the KDF input.
@param out Output buffer receiving the CMAC block.
@return 0 on success; -E2BIG if the assembled KDF input exceeds the internal buffer; propagated
CMAC error otherwise.

**called by** `ccc_derive_mupsk1`, `ccc_derive_mupsk2`, `ccc_derive_mursk`, `ccc_derive_salted_hash`, `ccc_derive_uad`, `ccc_derive_ursk_kt`, `derive_dkey`  ·  **calls** `ccc_aes_cmac`, `put_be32`

### `int ccc_derive_mupsk1(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MUPSK1_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:217`

@brief Derive mUPSK1, the SP0 Pre-POLL AES-CCM* key.

**calls** `kdf108_block`

### `int ccc_derive_mupsk2(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MUPSK2_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:229`

@brief Derive mUPSK2, the seed for the UWB-address KDF.

**calls** `kdf108_block`

### `int ccc_derive_mursk(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MURSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:249`

@brief Derive mURSK, the ranging-key seed feeding URSK_KT.

**calls** `kdf108_block`

### `int ccc_derive_salted_hash(const uint8_t ursk[CCC_URSK_LEN], const uint8_t *ranging_config, size_t rc_len, uint8_t out[CCC_SALTED_HASH_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:268`

@brief Derive SaltedHash from the serialized ranging configuration.

**calls** `ccc_aes_cmac`, `kdf108_block`

### `int ccc_derive_ursk_kt(const uint8_t mursk[CCC_MURSK_LEN], uint32_t sts_index, uint8_t out[CCC_URSK_KT_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:294`

@brief Derive URSK_KT, generated once per ranging cycle and keyed by the STS index.

**calls** `kdf108_block`

### `static int derive_dkey(const uint8_t ursk_kt[CCC_URSK_KT_LEN], const uint8_t label[4], const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint8_t out[CCC_DURSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:326`

@brief Shared dURSK/dUDSK derivation body: CMAC(URSK_KT, ctr1 || label || 0x00 || SaltedHash ||
0x000000 || 0x80).
@param ursk_kt URSK_KT input key for the current ranging cycle.
@param label 4-byte KDF label distinguishing dURSK from dUDSK.
@param salted_hash SaltedHash of the ranging configuration.
@param out Output buffer receiving the derived key.
@return Result of the underlying KDF block derivation.

**called by** `ccc_derive_dudsk`, `ccc_derive_dursk`  ·  **calls** `kdf108_block`

### `int ccc_derive_dursk(const uint8_t ursk_kt[CCC_URSK_KT_LEN], const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint8_t out[CCC_DURSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:340`

@brief Derive dURSK, per-cycle STS key material.

**calls** `derive_dkey`

### `int ccc_derive_dudsk(const uint8_t ursk_kt[CCC_URSK_KT_LEN], const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint8_t out[CCC_DUDSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:352`

@brief Derive dUDSK, per-cycle SP0 timestamp-frame key.

**calls** `derive_dkey`

### `int ccc_derive_sts_v(const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint32_t sts_index, uint8_t out[CCC_STS_V_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:364`

@brief Derive STS-V (phyHrpUwbStsV), the per-PPDU STS IV for the DW3000.

**calls** `get_be32`, `put_be32`

### `int ccc_derive_uad(const uint8_t mupsk2[CCC_MUPSK2_LEN], uint32_t sts_index0, uint8_t out[CCC_UAD_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:380`

@brief Derive UAD, the raw UWB-address derivation output.

**calls** `kdf108_block`, `put_be32`

### `static void remap_if_reserved(uint8_t *addr, size_t len)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:399`

@brief If addr is a reserved all-ones value (0xFFFF/0xFFFE for 2 bytes, 0xFF..FF for other
lengths), clear its top bit.
@param addr Address buffer checked and modified in place.
@param len Length of addr in bytes.

**called by** `ccc_uad_addresses`

### `int ccc_uad_addresses(const uint8_t uad[CCC_UAD_LEN], uint8_t keysource[CCC_KEYSOURCE_LEN], uint8_t dest_short_addr[CCC_DEST_SHORT_ADDR_LEN], uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:425`

@brief Split UAD into the UWB addresses (KeySource, destination short address, source long
address).

**calls** `remap_if_reserved`

### `static void sp0_nonce(uint8_t nonce[CCC_SP0_NONCE_LEN], const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN], uint32_t frame_counter)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:469`

@brief Build the SP0 CCM* nonce: SrcLongAddr || FrameCounter(BE) || SecLevel.
@param nonce Output buffer receiving the assembled nonce.
@param src_long_addr Source long address included in the nonce.
@param frame_counter Frame counter encoded big-endian into the nonce.

**called by** `ccc_sp0_decrypt`, `ccc_sp0_encrypt`  ·  **calls** `put_be32`

### `static int sp0_cbc_mac(const uint8_t key[CCC_MUPSK1_LEN], const uint8_t nonce[CCC_SP0_NONCE_LEN], const uint8_t *mhr, size_t mhr_len, const uint8_t *payload, size_t payload_len, uint8_t tag[AES_BLOCK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:489`

@brief Compute the CCM* CBC-MAC over B0 || l(a)||MHR || payload, zero-padded per block.
@param key AES-128 key used for the CBC-MAC.
@param nonce SP0 CCM* nonce for this frame.
@param mhr MAC header bytes forming the additional authenticated data (may be omitted if mhr_len
is 0).
@param mhr_len Length of mhr in bytes.
@param payload Payload bytes to authenticate.
@param payload_len Length of payload in bytes.
@param tag Output buffer receiving the computed CBC-MAC tag.
@return 0 on success; propagated error from AES encryption otherwise.

**called by** `ccc_sp0_decrypt`, `ccc_sp0_encrypt`  ·  **calls** `xor_bytes`

### `static int sp0_ctr(const uint8_t key[CCC_MUPSK1_LEN], const uint8_t nonce[CCC_SP0_NONCE_LEN], const uint8_t *in, size_t len, uint8_t *out, uint8_t s0[AES_BLOCK_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:559`

@brief Apply CCM* CTR mode: emit the S0 keystream block, then XOR keystream S1.. over in to out
(symmetric encrypt/decrypt).
@param key AES-128 key used for the CTR keystream.
@param nonce SP0 CCM* nonce for this frame.
@param in Input bytes to transform.
@param len Length of in/out in bytes.
@param out Output buffer receiving the transformed bytes.
@param s0 Output buffer receiving the S0 keystream block (used for MIC encryption).
@return 0 on success; propagated error from AES encryption otherwise.

**called by** `ccc_sp0_decrypt`, `ccc_sp0_encrypt`  ·  **calls** `xor_bytes`

### `static int sp0_ct_diff(const uint8_t *a, const uint8_t *b, size_t n)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:592`

@brief Constant-time inequality: 0 iff the @p n bytes are equal.

**called by** `ccc_sp0_decrypt`

### `int ccc_sp0_encrypt(const uint8_t key[CCC_MUPSK1_LEN], const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN], uint32_t frame_counter, const uint8_t *mhr, size_t mhr_len, const uint8_t *payload, size_t payload_len, uint8_t *ciphertext_out, uint8_t mic_out[CCC_SP0_MIC_LEN])`
`modules/woz_uwb/src/ccc/ccc_kdf.c:602`

@brief Encrypt + authenticate an SP0 data frame (AES-CCM*, ENC-MIC-64).

**calls** `sp0_cbc_mac`, `sp0_ctr`, `sp0_nonce`, `xor_bytes`

### `int ccc_sp0_decrypt(const uint8_t key[CCC_MUPSK1_LEN], const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN], uint32_t frame_counter, const uint8_t *mhr, size_t mhr_len, const uint8_t *ciphertext, size_t ciphertext_len, const uint8_t mic[CCC_SP0_MIC_LEN], uint8_t *payload_out)`
`modules/woz_uwb/src/ccc/ccc_kdf.c:634`

@brief Decrypt + verify an SP0 data frame; zeroes plaintext and returns -EBADMSG on MIC failure.

**calls** `sp0_cbc_mac`, `sp0_ct_diff`, `sp0_ctr`, `sp0_nonce`, `xor_bytes`
