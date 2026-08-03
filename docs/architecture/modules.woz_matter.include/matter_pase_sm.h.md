<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_pase_sm.h`

@file matter_pase_sm.h — PASE responder: the device side of the five messages.
matter_pase.h is the codec and matter_spake2p.h is the arithmetic; this is
what drives them. A commissioner opens with PBKDFParamRequest and this
answers, receives Pake1, answers Pake2, receives Pake3, and ends with a
StatusReport. What comes out the far side is a session key schedule.
-> PBKDFParamRequest    <- PBKDFParamResponse   (context hash fixed here)
-> Pake1 (pA)           <- Pake2 (pB, cB)
-> Pake3 (cA)           <- StatusReport(success)
The device never holds the setup passcode. It holds the SPAKE2+ verifier --
w0 and L -- which is derived from the passcode somewhere else and provisioned
in. That is the whole point of the augmented form: someone who reads the
device's flash cannot impersonate a commissioner to it.
No time and no randomness are taken from the environment. Retransmission is
MRP's job (matter_mrp.h), and the two random values PASE needs are arguments,
so the host suite runs the real state machine against a recorded exchange
rather than against whatever entropy it happened to get.

**depends on** [`modules/woz_matter/include/matter_crypto.h`](matter_crypto.h.md), [`modules/woz_matter/include/matter_pase.h`](matter_pase.h.md), [`modules/woz_matter/include/matter_spake2p.h`](matter_spake2p.h.md), [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/src/matter_pase_sm.c`](../modules.woz_matter.src/matter_pase_sm.c.md)

## API

### `struct matter_pase_verifier`
`modules/woz_matter/include/matter_pase_sm.h:99`

The provisioned SPAKE2+ verifier, plus the PBKDF parameters that produced it.
All four travel together because they have to agree: w0 and L are what
PBKDF2(passcode, salt, iterations) yields, and the salt and iteration count go
out in PBKDFParamResponse so the commissioner can repeat the derivation. A
verifier stored without the parameters that made it is unusable.
Deriving this needs a scalar multiply against the P-256 base point to get
L = w1*G, which is not one of the four operations nrf_oberon exposes here, so
it is generated off-device and provisioned -- which is how Matter intends it
anyway (the passcode is on the label, the verifier is in the flash).

### `struct matter_pase_responder`
`modules/woz_matter/include/matter_pase_sm.h:124`

PASE responder state machine: tracks the verifier, session IDs, ephemeral scalar y, random nonce,
SPAKE2+ context, expected commitment cA, shared secret ke, derived session keys, and the 534-byte
transcript live only during Pake1 handling.

### `matter_pase_responder_state`
`modules/woz_matter/include/matter_pase_sm.h:204`

@return the current state; MATTER_PASE_ST_DONE means keys are usable.
