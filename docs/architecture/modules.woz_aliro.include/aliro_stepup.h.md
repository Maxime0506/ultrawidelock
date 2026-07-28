<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_stepup.h`

Aliro step-up (Access Document) phase: builds the mdoc DeviceRequest, unwraps and decrypts the
SessionData DeviceResponse, decodes the CBOR document per spec 7.2/8.4.2, and runs the six-step
Access Document verification of spec 7.4. Reference-completeness codec + verifier; the verdict is
logged and stored, never gates the unlock (the provisioned trust store remains the sole gate).

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](aliro_crypto.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](../modules.woz_aliro.src/aliro_stepup.c.md), [`modules/woz_aliro/src/aliro_stepup_parse.c`](../modules.woz_aliro.src/aliro_stepup_parse.c.md)

## API

### `struct aliro_stepup_digest`
`modules/woz_aliro/include/aliro_stepup.h:96`

Step-up credential element digest: SHA-256 hash of a disclosed IssuerSignedItem, with its
digest_id for verification.

### `struct aliro_stepup_item`
`modules/woz_aliro/include/aliro_stepup.h:105`

Single disclosed IssuerSignedItem from a Step-up document: digest_id (which digest to check
against), tagged (24(bstr(IssuerSignedItem)) bytes for hashing), elem_id (element name).

### `struct aliro_stepup_doc`
`modules/woz_aliro/include/aliro_stepup.h:121`

Parsed Step-up access document (ISO/IEC 18013-5 mDoc): have_document (0 if device declined),
status (DeviceResponse "3" code), doc_type and name_space (issuer namespace), IssuerAuth
COSE_Sign1 components (protected header, kid, x5chain, payload=24(bstr(MSO)), signature r||s),
MobileSecurityObject (digest algorithm, doc type, disclosed digests), validity dates
(signed/valid_from/valid_until as epoch seconds, with flags), and disclosed IssuerSignedItems
(elem_id, tagged bytes, digest_id for matching against MSO).

### `struct aliro_stepup_issuer`
`modules/woz_aliro/include/aliro_stepup.h:163`

---- verifier (§7.4) ----

### `struct aliro_stepup_verify_ctx`
`modules/woz_aliro/include/aliro_stepup.h:174`

Context for Step-up document verification: issuers (trust store), time_valid/now_epoch (clock
state), access_iteration (stored iteration for replay check), expected_doctype, ecdsa_verify
callback (ES256 over message, takes 65-byte pub, message, 64-byte r||s sig, returns 0 on valid).

### `struct aliro_stepup_verdict`
`modules/woz_aliro/include/aliro_stepup.h:193`

Verdict of Step-up document verification (ISO/IEC 18013-5 §7.4): valid (all passed steps + >=1
valid element), reject_step (0=accepted, else step 1-6 that failed),
issuer_key_found/issuer_chain_validated/sig_ok/digests_ok/doctype_ok/time_ok/iteration_ok
(per-step flags), valid_elements (count of disclosed items with verified digest).

### `struct aliro_stepup_job`
`modules/woz_aliro/include/aliro_stepup.h:228`

---- ESP worker seam (implemented per-platform; see aliro_stepup_worker.c) ----
Copies the collected SessionData response + keys + verify inputs and runs
aliro_stepup_run() off the BLE-host task, so parse/verify never touches the
auth segment or the ranging arm window. Returns 0 if queued.
