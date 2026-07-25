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

### `void CreateSessionWork(k_work *)`
`modules/woz_nfc/src/transport_pn532.cpp:84`

Workqueue handler that creates an Aliro session for the NFC connection.

### `void RxWork(k_work *)`
`modules/woz_nfc/src/transport_pn532.cpp:93`

Workqueue handler that delivers buffered NFC receive data (sRxBuf, sRxLen) to the Aliro stack's
session handler.

### `void DestroySessionWork(k_work *)`
`modules/woz_nfc/src/transport_pn532.cpp:102`

Workqueue handler that destroys the Aliro session for the NFC connection.

### `void ArmEcpFrame()`
`modules/woz_nfc/src/transport_pn532.cpp:116`

Populate the ECP (Emulated Card Proximity) Aliro frame with header, reader identifier, and CRC_A
checksum. Sets sEcpArmed to true and logs the frame for debug. Caller must call this before
BroadcastEcp.

**called by** `Start`

### `void BroadcastEcp()`
`modules/woz_nfc/src/transport_pn532.cpp:140`

Fire-and-forget ECP beacon: CRC_A is precomputed in the frame, so the CIU
CRC engines are switched off around a raw InCommunicateThru. The expected
outcome is a chip-side timeout — nothing answers an ECP broadcast.

**called by** `PollRound`

### `int ExchangeApdu(const pn532_target &target, size_t &wireTxLen)`
`modules/woz_nfc/src/transport_pn532.cpp:172`

Adapt one stack-level APDU to the PN532's local limits.  Intermediate 9000
responses belong to transport-created ENVELOPE fragments and are therefore
consumed here; the Aliro stack sees exactly one response to the APDU it sent.

**called by** `RunSession`

### `void RunSession(const pn532_target &target)`
`modules/woz_nfc/src/transport_pn532.cpp:212`

One activated-device session: forward APDUs from Send() until the stack
terminates the session, polling stops, or an exchange fails.

**called by** `PollRound`  ·  **calls** `ExchangeApdu`

### `void PollRound()`
`modules/woz_nfc/src/transport_pn532.cpp:267`

Run one RF poll cycle: enable field, broadcast ECP beacon, detect ISO-DEP targets for 400 ms, and
activate a session if exactly one ISO-DEP card is found. Disable field and sleep for
CONFIG_WOZ_NFC_PN532_POLL_PERIOD_MS before returning. Called by ThreadMain in a loop when polling
is active.

**called by** `ThreadMain`  ·  **calls** `BroadcastEcp`, `RunSession`

### `void ThreadMain(void *, void *, void *)`
`modules/woz_nfc/src/transport_pn532.cpp:299`

Main loop of the PN532 polling thread: repeatedly run PollRound while sStarted is set, or park
the RF field and wait on sWakeSem with 500 ms timeout when stopped. Runs at preemptive
priority 7.

**calls** `PollRound`

### `AliroError Init()`
`modules/woz_nfc/src/transport_pn532.cpp:328`

Initialize the PN532 NFC reader on SPI (spi1) with retries and RF configuration. Probes firmware
version up to 3 times with 10 ms delay between attempts. Creates and names the polling thread.
Returns ALIRO_NO_ERROR on success; logs specific wiring and configuration hints on failure.

### `AliroError Start()`
`modules/woz_nfc/src/transport_pn532.cpp:394`

Start NFC polling: arm the ECP frame, set sStarted, and wake the polling thread. Returns
ALIRO_ERROR_INTERNAL if not initialized; otherwise ALIRO_NO_ERROR and logs "PN532 polling
started".

**calls** `ArmEcpFrame`

### `AliroError Stop()`
`modules/woz_nfc/src/transport_pn532.cpp:409`

Stop NFC polling by clearing sStarted and waking the polling thread. Returns ALIRO_NO_ERROR.

### `AliroError Send(Aliro::Data data)`
`modules/woz_nfc/src/transport_pn532.cpp:421`

Queue outbound APDU data for transmission to the active NFC tag. Fails if no tag is active, data
pointer is null, length is zero or exceeds buffer, or a previous send is still pending. Sets
sTxPending and wakes the polling thread.

### `AliroError Terminate()`
`modules/woz_nfc/src/transport_pn532.cpp:446`

Request termination of the PN532 polling thread and cleanup. Sets sTerminateReq and wakes the
polling thread. Returns ALIRO_NO_ERROR.
