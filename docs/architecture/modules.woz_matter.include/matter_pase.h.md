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

<details><summary>Undocumented (5)</summary>

- `matter_pase_pbkdf_req`
- `matter_pase_pbkdf_resp`
- `matter_pase_pake1`
- `matter_pase_pake2`
- `matter_pase_pake3`

</details>
