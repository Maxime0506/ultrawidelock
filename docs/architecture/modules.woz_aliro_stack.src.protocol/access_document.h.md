<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/access_document.h`

@file access_document.h
Aliro access document parsed from CBOR and COSE_Sign1 envelope: device public key, issued data
element, issuer-signed item, signature, issuer key ID and certificate, validity period, and
optional iteration count.

**used by** [`modules/woz_aliro_stack/src/protocol/access_document.c`](access_document.c.md), [`modules/woz_aliro_stack/src/session.cpp`](../modules.woz_aliro_stack.src/session.cpp.md)

## API

### `struct woz_aliro_access_document`
`modules/woz_aliro_stack/src/protocol/access_document.h:23`

Parsed Aliro access document with all fields needed for signature verification: device public
key, the issued data element and its issuer-signed item (CBOR), cryptographic envelope
(COSE_Sign1 protected/payload/signature), issuer key ID and certificate, validity timestamps, and
validity iteration if present.
