<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_case.h`

@file matter_case.h — proving an operational identity, both ways.
PASE let a commissioner in because it knew a printed code. CASE is what
happens afterwards, every time: two nodes that already hold certificates from
the same fabric prove it to each other and agree on session keys. It is the
only session type the spec will accept CommissioningComplete over, and the
only way a phone talks to this node once BLE is gone.
Sigma1  initiator -> responder   who I want, and my ephemeral key
Sigma2  responder -> initiator   my certificate chain, signed, encrypted
Sigma3  initiator -> responder   the same, in the other direction
This file is the responder's half, built in that order.
The subtle piece is Sigma1's destinationId. It is not an address: it is an
HMAC that only somebody holding the fabric's identity protection key could
have produced, over the identity they are asking for. A responder does not
read a node id out of it -- it recomputes the HMAC for each fabric it holds
and looks for a match. That is what makes an unsolicited Sigma1 unable to
enumerate a node's fabrics: get the key wrong and you learn nothing.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md)

## API

### `struct matter_case_sigma1`
`modules/woz_matter/include/matter_case.h:100`

What Sigma1 carries. Pointers borrow the caller's buffer; nothing is copied.

### `struct matter_case_sigma2_in`
`modules/woz_matter/include/matter_case.h:138`

What building a Sigma2 needs, and nothing it can derive for itself.
Gathered into one struct because the alternative is an eleven-argument
function whose adjacent 32-byte buffers can be swapped without the compiler
noticing -- and two of them, the random and the transcript hash, are both
32 bytes and both feed the same salt.

### `struct matter_case_sigma3_out`
`modules/woz_matter/include/matter_case.h:195`

Who the Sigma3 proved its sender to be.

### `struct matter_case_sigma3_in`
`modules/woz_matter/include/matter_case.h:203`

What opening a Sigma3 needs, all of it already in hand by then.
