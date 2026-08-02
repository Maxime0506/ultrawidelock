<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_fabric.h`

@file matter_fabric.h — the operational identity a commissioner installs.
Attestation ends with the commissioner holding a public key this node proved
it owns. What follows is the commissioner handing back an identity built on
that key:
AddTrustedRootCertificate  trust this root
AddNOC                     and here is who you are underneath it
Both certificates arrive as MATTER TLV, not X.509. The spec defines a
compressed form precisely so a constrained node can read one without an
ASN.1 decoder, and this file is that reader.
It reads three things and ignores the rest: the subject's node id, its fabric
id, and the public key. Validity dates, key usage and the signature are what
a node checks when VERIFYING a certificate somebody else presents, which is
CASE's job. A commissioner has no reason to lie to itself about a NOC it just
minted, and this node cannot check the signature anyway without the issuer's
key -- which, for the NOC, is the root it was told to trust one command
earlier and has no independent reason to believe.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_clusters.h`](matter_clusters.h.md), [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md), [`modules/woz_matter/src/matter_fabric.c`](../modules.woz_matter.src/matter_fabric.c.md)

## API

### `struct matter_cert_info`
`modules/woz_matter/include/matter_fabric.h:74`

What matter_cert_parse() found. Absent fields leave their have_* flag false.

### `struct matter_fabric`
`modules/woz_matter/include/matter_fabric.h:106`

One fabric's worth of operational identity.
Held in RAM and nothing more. A fabric is supposed to survive a reboot, and
this one does not; there is no settings backend on this port yet, and the
fail-safe would roll an incomplete commissioning back regardless. What it
does have to survive is the gap between AddNOC and CASE, which is the same
boot.
The trusted root is kept as a public key rather than as the certificate it
arrived in: verifying a peer's NOC chain needs the key, and nothing this node
does needs the other 300-odd bytes. The ICAC is kept whole because CASE has
to send it back out.

### `struct matter_icac_slot`
`modules/woz_matter/include/matter_fabric.h:151`

The one intermediate certificate this node can hold, and whose it is.
@param owner_index the fabric index holding it, or 0 when free.
