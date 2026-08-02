<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_attest.h`

@file matter_attest.h — proving to a commissioner that this is a real device.
After the fail-safe is armed, the commissioner stops asking what this node is
and starts asking it to prove it. Three questions, in this order:
CertificateChainRequest  give me your DAC, then your PAI
AttestationRequest       sign this nonce with the DAC's private key
CSRRequest               make me a key I can certify, and sign for it
The certificates are static blobs. The signatures are not: each covers the
message AND the session's attestation challenge, which is why a recorded
exchange cannot be replayed into a different session.
WHAT THESE CREDENTIALS ARE. The DAC, PAI and CD here are the SDK's published
development credentials for vendor 0xFFF1 / product 0x8001, and the DAC's
private key is published alongside them. They prove nothing about who built
this device -- anyone can extract the same key from a public repository, and
a commissioner that enforces attestation will reject them. They are here so
commissioning can be developed against a real phone; shipping a product means
a DAC issued under a real PAI, and its private key must not live in flash
next to the certificate.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_clusters.h`](matter_clusters.h.md), [`modules/woz_matter/src/matter_attest.c`](../modules.woz_matter.src/matter_attest.c.md)
