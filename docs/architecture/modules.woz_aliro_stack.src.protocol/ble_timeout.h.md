<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/ble_timeout.h`

Aliro 1.0 Bluetooth LE responseTimeout rules (section 11.9).

**used by** [`modules/woz_aliro_stack/src/protocol/ble_timeout.c`](ble_timeout.c.md), [`modules/woz_aliro_stack/src/session.cpp`](../modules.woz_aliro_stack.src/session.cpp.md)

## API

### `struct woz_aliro_ble_timeout_state`
`modules/woz_aliro_stack/src/protocol/ble_timeout.h:62`

BLE timeout state machine: tracks the current role (reader or mobile) and which message (if any)
is pending a response. Used to enforce timeouts on authentication steps.
