<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_case.c`

*No module docstring. First commit: "woz_matter: CASE, the responder's first half".*

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_case.h`](../modules.woz_matter.include/matter_case.h.md), [`modules/woz_matter/include/matter_crypto.h`](../modules.woz_matter.include/matter_crypto.h.md), [`modules/woz_matter/include/matter_fabric.h`](../modules.woz_matter.include/matter_fabric.h.md), [`modules/woz_matter/include/matter_im.h`](../modules.woz_matter.include/matter_im.h.md), [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `int matter_case_operational_ipk(const uint8_t epoch_key[MATTER_CASE_IPK_LEN], const uint8_t compressed_fabric_id[8], uint8_t out[MATTER_CASE_IPK_LEN])`
`modules/woz_matter/src/matter_case.c:27`

Derive the CASE operational IPK from an epoch key and compressed fabric ID using HKDF with the
"GroupKey v1.0" info string; returns MATTER_OK on success.

### `int matter_case_destination_id(const uint8_t ipk[MATTER_CASE_IPK_LEN], const uint8_t initiator_random[MATTER_CASE_RANDOM_LEN], const uint8_t root_pub[MATTER_CASE_PUBKEY_LEN], uint64_t fabric_id, uint64_t node_id, uint8_t out[MATTER_CASE_DEST_ID_LEN])`
`modules/woz_matter/src/matter_case.c:50`

Compute a CASE destination ID by hashing the initiator random, root public key, fabric ID, and
node ID with the IPK as the HMAC key; returns MATTER_OK on success.

### `static int take_bytes(const struct matter_tlv_reader *r, const uint8_t **out, size_t want)`
`modules/woz_matter/src/matter_case.c:82`

Borrow one octet string of an expected length out of the loaded element.

**called by** `matter_case_sigma1_decode`

### `int matter_case_sigma1_decode(const uint8_t *tlv, size_t len, struct matter_case_sigma1 *out)`
`modules/woz_matter/src/matter_case.c:102`

Decode a Sigma1 message from TLV, extracting initiator random, destination ID, initiator public
key, session ID, and optional resumption ID; skips unknown fields and returns MATTER_E_INVAL if
mandatory fields are missing.

**calls** `take_bytes`

### `int matter_case_sigma2_encode(const struct matter_case_sigma2_in *in, uint8_t *out, size_t cap, size_t *out_len, uint8_t shared_out[MATTER_CASE_SECRET_LEN])`
`modules/woz_matter/src/matter_case.c:223`

Encode a Sigma2 message by computing ECDH, deriving S2K from a salt, signing TBSData2, encrypting
TBEData2, and wrapping both in TLV with session parameters; returns MATTER_OK on success.

### `int matter_case_sigma3_open(const struct matter_case_sigma3_in *in, const uint8_t *tlv, size_t len, struct matter_case_sigma3_out *out)`
`modules/woz_matter/src/matter_case.c:385`

Open and validate a Sigma3 message by decrypting TBEData3, verifying the signature over TBSData3
against the initiator's NOC, and extracting the node ID, fabric ID, and public key; returns
MATTER_OK on success.
