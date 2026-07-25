<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/transport_rfal.cpp`

WozNfc backend forwarding to the add-on's ST25R/RFAL transport unchanged.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](../modules.woz_nfc.include.woz_nfc/transport.h.md)

## API

### `AliroError Init()`
`modules/woz_nfc/src/transport_rfal.cpp:13`

Initialize the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Init().

### `AliroError Start()`
`modules/woz_nfc/src/transport_rfal.cpp:21`

Start the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Start().

### `AliroError Stop()`
`modules/woz_nfc/src/transport_rfal.cpp:29`

Stop the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Stop().

### `AliroError Send(Aliro::Data data)`
`modules/woz_nfc/src/transport_rfal.cpp:37`

Send data on the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Send(data).

### `AliroError Terminate()`
`modules/woz_nfc/src/transport_rfal.cpp:45`

Terminate the RFAL NFC transport by delegating to NfcTransportRfal::Instance().Terminate().
