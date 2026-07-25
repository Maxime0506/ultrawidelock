<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_prov.c`

Aliro reader provisioning state: default dev identity, and serialization/deserialization of the
reader identity plus trusted-credential store to/from a self-describing binary blob.
Also implements the trust-store membership check and add-with-dedup operations used to decide
whether a presented credential public key is trusted.

**depends on** [`modules/woz_aliro/include/aliro_prov.h`](../modules.woz_aliro.include/aliro_prov.h.md)  ·  **discussed in** [`docs/porting-esp32-phase3.md`](../../porting-esp32-phase3.md), [`ports/esp32/components/aliro_reader/README.md`](../../../ports/esp32/components/aliro_reader/README.md)

## API

### `void aliro_prov_dev_default(struct aliro_reader_identity *id, struct aliro_trust_store *ts)`
`modules/woz_aliro/src/aliro_prov.c:46`

Load the built-in development reader identity and empty trust store (zeroed issuer credentials
and kpersistent keys).

### `int aliro_prov_serialize(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_aliro/src/aliro_prov.c:64`

Serialize reader identity and trust store into a provisioning blob (v3 format with grk and
kpersistent). Returns 0 on success, -1 if count exceeds ALIRO_TRUST_MAX or buffer too small.
Outputs blob and sets out_len.

### `int aliro_prov_deserialize(const uint8_t *buf, size_t len, struct aliro_reader_identity *id, struct aliro_trust_store *ts)`
`modules/woz_aliro/src/aliro_prov.c:123`

Deserialize a provisioning blob (magic + version + flags + reader_id + sign_priv + grk +
credential count + cred_pub list + kpersistent bitmask + kpersistent list). Supports v1 (no grk),
v2 (grk, no kpersistent), v3 (both). Returns 0 on success, -1 on invalid
magic/version/length/count.

### `int aliro_prov_trust_check(const struct aliro_trust_store *ts, const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])`
`modules/woz_aliro/src/aliro_prov.c:201`

Check if a credential public key is in the trust store: returns 0 (trusted), -1 (known set, not a
member), 1 (no anchors provisioned). The return order (0, -1, 1) matches the credential state
(accepted, rejected, uncertain).

**called by** `aliro_prov_trust_add`

### `int aliro_prov_trust_add(struct aliro_trust_store *ts, const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])`
`modules/woz_aliro/src/aliro_prov.c:221`

Add a credential public key to the trust store if not already present. Returns 0 on success, 1 if
already present, -1 on error (null pointer, invalid key format, or store full). The key must
start with 0x04 (uncompressed point). Clears the Kpersistent bit for the new slot and zeroes its
entry.

**calls** `aliro_prov_trust_check`

### `int aliro_prov_trust_find(const struct aliro_trust_store *ts, const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])`
`modules/woz_aliro/src/aliro_prov.c:244`

Find the index of a credential public key in the trust store. Returns the index (0..count-1) on
match, -1 if not found or ts is NULL.

### `int aliro_prov_kpersistent_set(struct aliro_trust_store *ts, int idx, const uint8_t kp[ALIRO_KPERSISTENT_LEN])`
`modules/woz_aliro/src/aliro_prov.c:262`

Set the Kpersistent key for a credential at index idx; marks it valid in the bitmask. Returns 0
on success, -1 if idx out of range.
