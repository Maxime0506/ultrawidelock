<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_clusters.h`

@file matter_clusters.h — what this device answers, as opposed to how.
matter_im.c owns the ReportData wire format and knows nothing about door
locks or vendor IDs. This is the other half: the endpoints, clusters and
attributes that exist, and what they say.
Scope is deliberately the commissioner's FIRST question and no further. A
real iPhone, immediately after PASE, reads nine attribute paths:
endpoint 0  GeneralCommissioning 0x0030  attributes 0x00..0x04 and 0x0C
endpoint 0  BasicInformation     0x0028  VendorID 0x02, ProductID 0x04
endpoint 0  TimeSynchronization  0x0038  all attributes (wildcard)
Everything else answers UNSUPPORTED_*, which is a legal answer and a truthful
one. Clusters get added when a commissioner is observed asking for them,
rather than because the spec lists them.
Device-specific values arrive in @ref matter_device_info instead of being
read from Kconfig here, so the host suite can build this without Zephyr and
assert on the encoded bytes.

**depends on** [`modules/woz_matter/include/matter_attest.h`](matter_attest.h.md), [`modules/woz_matter/include/matter_fabric.h`](matter_fabric.h.md), [`modules/woz_matter/include/matter_im.h`](matter_im.h.md), [`modules/woz_matter/include/matter_thread.h`](matter_thread.h.md)  ·  **used by** [`modules/woz_matter/src/matter_clusters.c`](../modules.woz_matter.src/matter_clusters.c.md)

## API

### `struct matter_user`
`modules/woz_matter/include/matter_clusters.h:261`

One user slot. Reported by GetUser, filled by SetUser.

### `struct matter_device_info`
`modules/woz_matter/include/matter_clusters.h:497`

Complete device information structure held by the Matter node, including vendor/product IDs,
Aliro identity, user table, commissioning state, operational network configuration, and
session-specific attestation and key data.

### `struct matter_admin_hooks`
`modules/woz_matter/include/matter_clusters.h:752`

What the application must do when a controller opens a commissioning window.
The cluster decodes and validates; everything it would then have to TOUCH --
the SPAKE2+ verifier the PASE responder uses, the BLE advertising payload,
the expiry timer -- belongs to the port. So this module stays free of both
Bluetooth and Zephyr, which is what lets tests/host compile it.
All three return a MATTER_ADMIN_STATUS_* code, or 0 for success.
