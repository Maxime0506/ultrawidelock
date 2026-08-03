<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_device.c`

Aliro initiator (User-Device) session machine: the implementation behind
aliro_device.h. Feeds one reader command at a time through
aliro_device_on_command, which parses AUTH0/AUTH1/EXCHANGE with the inverse
codec, runs the mirror of the reader's key schedule (ephemeral ECDH, the two
ECDSA transcripts, the session salt) and returns the sealed response. Owns the
two AES-256-GCM channels the device holds, the Access-Protocol channel and the
BleSK ranging channel, both split out of the same 160-byte key block, plus the
standard-path derivation factored EC-free so host tests can drive it with a
supplied shared secret.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](../modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_device.h`](../modules.woz_aliro.include/aliro_device.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/src/aliro_apdu.h`](aliro_apdu.h.md)

## API

### `void aliro_dev_secchan_init(struct aliro_dev_secchan *sc, const uint8_t s0[32], const uint8_t s1[32])`
`modules/woz_aliro/src/aliro_device.c:43`

Initialize a secure channel with two keys and both counters to 1 (per §8.3.1.13).

**called by** `aliro_dev_blesk_init`, `aliro_device_derive_session`

### `int aliro_dev_secchan_open(struct aliro_dev_secchan *sc, const uint8_t *ct, size_t ct_len, const uint8_t tag[16], uint8_t *pt)`
`modules/woz_aliro/src/aliro_device.c:56`

AES-256-GCM decrypt one reader->device message: derive nonce from counter, decrypt and verify
tag, increment counter, return 0 on success or -1 on tag mismatch.

**called by** `aliro_device_on_command`

### `int aliro_dev_secchan_seal(struct aliro_dev_secchan *sc, const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t tag[16])`
`modules/woz_aliro/src/aliro_device.c:74`

AES-256-GCM encrypt one device->reader message: derive nonce from counter, encrypt and compute
tag, increment counter, return 0 on success or -1 on error.

**called by** `aliro_device_on_command`

### `int aliro_dev_blesk_init(struct aliro_dev_secchan *ch, const uint8_t block[ALIRO_KEY_BLOCK_LEN], const uint8_t *versions_salt, size_t salt_len)`
`modules/woz_aliro/src/aliro_device.c:100`

Initialize a BLE secure channel by deriving session keys from a key block and versions salt, then
initializing the channel with both keys.

**called by** `aliro_device_on_command`  ·  **calls** `aliro_dev_secchan_init`

### `int aliro_dev_ble_open(struct aliro_dev_secchan *ch, const uint8_t *wire, size_t wire_len, uint8_t *plain, size_t plain_cap, size_t *plain_len)`
`modules/woz_aliro/src/aliro_device.c:118`

Decrypt one BLE-SK wire frame (reader->device direction): validate length header, derive nonce,
decrypt payload under S0 with AAD, increment counter, copy plaintext and return 0 on success or
-1 on length/tag error.

### `int aliro_dev_ble_seal(struct aliro_dev_secchan *ch, const uint8_t *plain, size_t plain_len, uint8_t *wire, size_t wire_cap, size_t *wire_len)`
`modules/woz_aliro/src/aliro_device.c:157`

Encrypt one BLE-SK wire frame (device->reader direction): validate plaintext length header,
derive nonce, encrypt payload under S1 with AAD, write wire length header, increment counter,
return 0 on success or -1 on length/tag error.

### `int aliro_dev_seal_cryptogram(const uint8_t cryptogram_sk[32], const uint8_t *plain, size_t plain_len, uint8_t *out)`
`modules/woz_aliro/src/aliro_device.c:196`

AES-256-GCM encrypt with all-zero 12-byte IV and no AAD; plaintext and ciphertext lengths must
match.

### `int aliro_device_derive_session(const uint8_t shared_x[32], const uint8_t txid[16], const uint8_t reader_group_x[32], const uint8_t reader_eph_x[32], const uint8_t reader_id[32], uint8_t exp_phase, const uint8_t *a5, size_t a5n, const uint8_t device_eph_x[32], struct aliro_dev_secchan *sc, uint8_t ursk[32], uint8_t block_out[ALIRO_KEY_BLOCK_LEN])`
`modules/woz_aliro/src/aliro_device.c:210`

Derive a session key block, URSK, and secure channel from an ECDH shared secret, transaction ID,
and reader identity by building a salt, deriving the block, and splitting keys; returns 0 on
success.

**called by** `aliro_device_on_command`  ·  **calls** `aliro_dev_secchan_init`

### `int aliro_device_init(struct aliro_device *d, const uint8_t cred_priv[32], const uint8_t reader_id[32], const uint8_t reader_verif_pub[65])`
`modules/woz_aliro/src/aliro_device.c:247`

Initialize device with access credential, reader identity and verification key; derive public key
from private scalar; set version to v1.0, BLE-SK salt to v1.0 single-version default, and phase
to idle. Return 0 on success or -1 if public key derivation fails.

### `int aliro_device_set_blesk_salt(struct aliro_device *d, const uint8_t *salt, size_t len)`
`modules/woz_aliro/src/aliro_device.c:277`

Set BLE-SK salt from big-endian u16 list; must be even length (at least 4 bytes) and fit in
buffer; return 0 on success or -1 if length or format is invalid.

### `int aliro_device_on_command(struct aliro_device *d, const uint8_t *ap_payload, size_t len, uint8_t *resp, size_t cap, size_t *resp_len)`
`modules/woz_aliro/src/aliro_device.c:296`

Process an incoming Aliro APDU command (AUTH0, AUTH1, or EXCHANGE) and generate the response.
Validates command structure, reader identity, signatures, and key derivation; returns 0 on
success and sets device phase accordingly. On any failure sets phase to ALIRO_DEV_FAILED and
returns -1.

**calls** `aliro_dev_blesk_init`, `aliro_dev_secchan_open`, `aliro_dev_secchan_seal`, `aliro_device_derive_session`
