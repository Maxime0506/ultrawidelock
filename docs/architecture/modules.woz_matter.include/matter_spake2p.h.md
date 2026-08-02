<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_spake2p.h`

@file matter_spake2p.h — SPAKE2+ glue: PBKDF2, transcript, confirmations.
SPAKE2+ is how PASE turns a short setup passcode into a session key without
ever putting the passcode on the wire. The elliptic-curve arithmetic is NOT
here: it comes from nrf_oberon, which ships four primitives that do exactly
the operations SPAKE2+ needs. Everything around them -- deriving w0 and w1
from the passcode, building the transcript, and turning it into the
confirmation values -- is this file, and all of it is byte manipulation and
hashing that the host suite can check.
w0, w1   PBKDF2-HMAC-SHA256(passcode, salt, iterations) -> 80 B -> two
40-byte halves, each reduced mod the P-256 group order
TT       ten elements, each prefixed with its length as a little-endian
uint64: context, "", "", M, N, pA, pB, Z, V, w0
Ka|Ke    SHA256(TT), first half and second half
KcA|KcB  HKDF(Ka, "ConfirmationKeys")
cA, cB   HMAC(KcA, pB) and HMAC(KcB, pA)

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_pase_sm.h`](matter_pase_sm.h.md), [`modules/woz_matter/src/matter_spake2p.c`](../modules.woz_matter.src/matter_spake2p.c.md)

## API

### `struct matter_spake2p_result`
`modules/woz_matter/include/matter_spake2p.h:157`

What the transcript yields: the two confirmations and the shared secret.
