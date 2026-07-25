<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/transport_pn532.cpp`

WozNfc backend driving an NXP PN532 reader.
A dedicated thread owns the chip: it runs the discovery loop (RF field on,
one Apple ECP broadcast, one 106 kbps type A activation attempt, field off,
sleep) and, once an ISO-DEP User Device is activated, performs the blocking
APDU round trips. Stack callbacks (CreateSession / HandleSessionData /
DestroySession) are posted to the Aliro workqueue so the stack observes the
same threading as with the upstream RFAL transport, and Send() stays
asynchronous: it hands the APDU to the thread and returns.
The ECP frame layout mirrors modules/woz_aliro_ecp (the RFAL-path emitter):
8-byte Aliro ECP v2 header, 8-byte provisioned reader identifier, CRC_A.
The PN532 cannot inject raw frames mid-discovery the way RFAL's proprietary
poll hook can, so the frame is broadcast with InCommunicateThru while the
CIU CRC is switched off, between activation attempts — the same cadence a
matching iPhone expects: ECP beacon, then WUPA.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](../modules.woz_nfc.include.woz_nfc/transport.h.md), [`modules/woz_nfc/src/pn532.h`](pn532.h.md), [`modules/woz_nfc/src/pn532_apdu.h`](pn532_apdu.h.md), [`modules/woz_nfc/src/pn532_bus.h`](pn532_bus.h.md)  ·  **discussed in** [`modules/woz_nfc/README.md`](../../../modules/woz_nfc/README.md)

## API

### `void BroadcastEcp()`
`modules/woz_nfc/src/transport_pn532.cpp:125`

Fire-and-forget ECP beacon: CRC_A is precomputed in the frame, so the CIU
CRC engines are switched off around a raw InCommunicateThru. The expected
outcome is a chip-side timeout — nothing answers an ECP broadcast.

**called by** `PollRound`

### `int ExchangeApdu(const pn532_target &target, size_t &wireTxLen)`
`modules/woz_nfc/src/transport_pn532.cpp:157`

Adapt one stack-level APDU to the PN532's local limits.  Intermediate 9000
responses belong to transport-created ENVELOPE fragments and are therefore
consumed here; the Aliro stack sees exactly one response to the APDU it sent.

**called by** `RunSession`

### `void RunSession(const pn532_target &target)`
`modules/woz_nfc/src/transport_pn532.cpp:197`

One activated-device session: forward APDUs from Send() until the stack
terminates the session, polling stops, or an exchange fails.

**called by** `PollRound`  ·  **calls** `ExchangeApdu`

<details><summary>Undocumented (11)</summary>

- `CreateSessionWork`
- `RxWork`
- `DestroySessionWork`
- `ArmEcpFrame`
- `PollRound`
- `ThreadMain`
- `Init`
- `Start`
- `Stop`
- `Send`
- `Terminate`

</details>
