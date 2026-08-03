<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_attest.c`

*No module docstring. First commit: "woz_matter: attestation, accepted by a real iPhone".*

**depends on** [`modules/woz_matter/include/matter_attest.h`](../modules.woz_matter.include/matter_attest.h.md), [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `int matter_attest_cert(uint8_t type, const uint8_t **out, size_t *len)`
`modules/woz_matter/src/matter_attest.c:152`

Retrieve a prepacked attestation certificate (DAC or PAI) by type. Returns MATTER_OK and fills
out and len on success, or MATTER_E_INVAL if type is not recognized or arguments are NULL.

### `int matter_attest_elements_encode(const uint8_t *nonce, size_t nonce_len, uint32_t timestamp, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_attest.c:175`

Encode and return the attestation elements (nonce, timestamp, certification declaration) as a TLV
structure. Validates nonce, timestamp, out, and out_len pointers. Returns MATTER_OK on success or
MATTER_E_INVAL if any pointer is NULL.

### `int matter_attest_nocsr_encode(const uint8_t *csr, size_t csr_len, const uint8_t *nonce, size_t nonce_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_attest.c:199`

Encode the CSR and nonce as a TLV-encoded CertificateChainResponse payload. Validates all input
pointers. Returns MATTER_OK on success or MATTER_E_INVAL if any pointer is NULL.

### `int matter_attest_sign_with_challenge(uint8_t *buf, size_t payload_len, size_t cap, const uint8_t *challenge, size_t challenge_len, uint8_t sig[MATTER_ATTEST_SIG_LEN])`
`modules/woz_matter/src/matter_attest.c:223`

Sign a payload plus challenge with the DAC private key. Appends the challenge to the buffer,
signs the combined data, and clears the challenge bytes before returning. Returns MATTER_OK on
success, MATTER_E_INVAL if any pointer is NULL, MATTER_E_NOSPACE if the buffer has insufficient
room, or MATTER_E_STATE if the signature operation fails.

### `struct der`
`modules/woz_matter/src/matter_attest.c:251`

A backwards DER writer: @ref pos walks down from the end of the buffer, and
everything already written lives in buf[pos..cap).

### `static void der_raw(struct der *d, const uint8_t *src, size_t len)`
`modules/woz_matter/src/matter_attest.c:262`

Prepend raw bytes to the DER encoding buffer. Marks the buffer bad if there is insufficient
space.

**called by** `der_byte`, `der_cri`, `der_int`, `matter_attest_csr`

### `static void der_byte(struct der *d, uint8_t b)`
`modules/woz_matter/src/matter_attest.c:276`

Append one byte to the DER encoding buffer by prepending it to the existing bytes. Marks buffer
bad if insufficient space.

**called by** `der_cri`, `der_hdr`, `der_int`, `matter_attest_csr`  ·  **calls** `der_raw`

### `static void der_hdr(struct der *d, uint8_t tag, size_t len)`
`modules/woz_matter/src/matter_attest.c:282`

Prepend a tag and the length of the @p len bytes already written.

**called by** `der_cri`, `der_int`, `matter_attest_csr`  ·  **calls** `der_byte`

### `static size_t der_len(const struct der *d)`
`modules/woz_matter/src/matter_attest.c:301`

How many bytes are written so far, for computing the enclosing length.

**called by** `der_cri`, `matter_attest_csr`

### `static void der_int(struct der *d, const uint8_t *v, size_t len)`
`modules/woz_matter/src/matter_attest.c:336`

Prepend one of the signature's two integers.
DER integers are SIGNED, so a 32-byte value whose top bit is set needs a
leading zero or it reads as negative -- and leading zero bytes must otherwise
be dropped. Getting either wrong produces a CSR that parses and fails to
verify.

**called by** `matter_attest_csr`  ·  **calls** `der_byte`, `der_hdr`, `der_raw`

### `static void der_cri(struct der *d, const uint8_t pub[65])`
`modules/woz_matter/src/matter_attest.c:369`

CertificationRequestInfo, the part that gets signed, INCLUDING its own
SEQUENCE wrapper.
Wrapping itself matters: on the second pass this is prepended to a buffer
that already holds the signature, so a caller adding the wrapper afterwards
would measure the whole buffer and enclose the signature as well.

**called by** `matter_attest_csr`  ·  **calls** `der_byte`, `der_hdr`, `der_len`, `der_raw`

<details><summary>Undocumented (1)</summary>

- `matter_attest_csr` — tested: matter attest

</details>
