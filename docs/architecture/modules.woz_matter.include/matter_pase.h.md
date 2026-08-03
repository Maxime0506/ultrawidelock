<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_pase.h`

@file matter_pase.h — PASE message codec (the five commissioning messages).
PASE is how a commissioner proves it knows the setup passcode. Five messages,
all Matter TLV structures on the Secure Channel protocol:
PBKDFParamRequest   initiatorRandom, initiatorSessionId, passcodeId,
hasPBKDFParameters, [initiatorSessionParams]
PBKDFParamResponse  initiatorRandom, responderRandom, responderSessionId,
[pbkdfParameters{iterations, salt}], [responderSessionParams]
Pake1               pA
Pake2               pB, cB
Pake3               cA
This file is the codec only. The SPAKE2+ arithmetic that produces pA/pB/cA/cB
is separate, and on this part it comes from nrf_oberon
(nrfxlib/crypto/nrf_oberon/include/ocrypto_spake2p_p256.h), which already
ships in every image here.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_pase_sm.h`](matter_pase_sm.h.md), [`modules/woz_matter/src/matter_pase.c`](../modules.woz_matter.src/matter_pase.c.md)

## API

### `struct matter_session_params`
`modules/woz_matter/include/matter_pase.h:80`

MRP parameters a peer advertises for itself. Absent means "use the defaults",
which is why presence is tracked rather than defaulted here.

### `struct matter_pase_pbkdf_req`
`modules/woz_matter/include/matter_pase.h:91`

PASE PBKDFParamRequest message holding initiator random, session ID, passcode ID, and optional
PBKDF session parameters.

### `struct matter_pase_pbkdf_resp`
`modules/woz_matter/include/matter_pase.h:103`

PASE PBKDFParamResponse message holding initiator and responder randoms, session ID, and optional
PBKDF parameters (iterations and salt) if the initiator did not already have them.

### `struct matter_pase_pake1`
`modules/woz_matter/include/matter_pase.h:124`

PASE Sigma1 message payload holding the initiator's ephemeral public key point.

### `struct matter_pase_pake2`
`modules/woz_matter/include/matter_pase.h:131`

PASE Sigma2 message payload holding the responder's ephemeral public key point and hash.

### `struct matter_pase_pake3`
`modules/woz_matter/include/matter_pase.h:139`

PASE Sigma3 message payload holding the initiator's hash for mutual authentication.
