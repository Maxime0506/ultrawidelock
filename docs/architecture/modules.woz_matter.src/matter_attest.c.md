<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_attest.c`

*No module docstring. First commit: "woz_matter: attestation, accepted by a real iPhone".*

**depends on** [`modules/woz_matter/include/matter_attest.h`](../modules.woz_matter.include/matter_attest.h.md), [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `struct der`
`modules/woz_matter/src/matter_attest.c:232`

A backwards DER writer: @ref pos walks down from the end of the buffer, and
everything already written lives in buf[pos..cap).

### `static void der_hdr(struct der *d, uint8_t tag, size_t len)`
`modules/woz_matter/src/matter_attest.c:255`

Prepend a tag and the length of the @p len bytes already written.

**called by** `der_cri`, `der_int`, `matter_attest_csr`  ·  **calls** `der_byte`

### `static size_t der_len(const struct der *d)`
`modules/woz_matter/src/matter_attest.c:274`

How many bytes are written so far, for computing the enclosing length.

**called by** `der_cri`, `matter_attest_csr`

### `static void der_int(struct der *d, const uint8_t *v, size_t len)`
`modules/woz_matter/src/matter_attest.c:309`

Prepend one of the signature's two integers.
DER integers are SIGNED, so a 32-byte value whose top bit is set needs a
leading zero or it reads as negative -- and leading zero bytes must otherwise
be dropped. Getting either wrong produces a CSR that parses and fails to
verify.

**called by** `matter_attest_csr`  ·  **calls** `der_byte`, `der_hdr`, `der_raw`

### `static void der_cri(struct der *d, const uint8_t pub[65])`
`modules/woz_matter/src/matter_attest.c:342`

CertificationRequestInfo, the part that gets signed, INCLUDING its own
SEQUENCE wrapper.
Wrapping itself matters: on the second pass this is prepended to a buffer
that already holds the signature, so a caller adding the wrapper afterwards
would measure the whole buffer and enclose the signature as well.

**called by** `matter_attest_csr`  ·  **calls** `der_byte`, `der_hdr`, `der_len`, `der_raw`

<details><summary>Undocumented (7)</summary>

- `matter_attest_cert` — tested: matter attest
- `matter_attest_elements_encode` — tested: matter attest
- `matter_attest_nocsr_encode` — tested: matter attest
- `matter_attest_sign_with_challenge` — tested: matter attest
- `der_raw`
- `der_byte`
- `matter_attest_csr` — tested: matter attest

</details>
