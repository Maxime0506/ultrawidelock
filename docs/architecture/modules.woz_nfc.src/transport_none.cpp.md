<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/transport_none.cpp`

WozNfc backend for boards with no NFC frontend: polling never starts and no
NFC session is ever created, so Send()/Terminate() are unreachable in a
correct run; Send() reports invalid state defensively.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](../modules.woz_nfc.include.woz_nfc/transport.h.md)

## API

### `AliroError Init()`
`modules/woz_nfc/src/transport_none.cpp:17`

Initialize the no-op NFC transport; always succeeds.

### `AliroError Start()`
`modules/woz_nfc/src/transport_none.cpp:26`

Start the no-op NFC transport; logs that NFC is disabled and returns success. Called during
system initialization when no NFC reader is fitted.

### `AliroError Stop()`
`modules/woz_nfc/src/transport_none.cpp:35`

Stop the no-op NFC transport; always succeeds.

### `AliroError Send(Aliro::Data)`
`modules/woz_nfc/src/transport_none.cpp:44`

Attempt to send data on the no-op NFC transport; always fails with ALIRO_INVALID_STATE because no
transport is active.

### `AliroError Terminate()`
`modules/woz_nfc/src/transport_none.cpp:52`

Terminate the no-op NFC transport; always succeeds.
