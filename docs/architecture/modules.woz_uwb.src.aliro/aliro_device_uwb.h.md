<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_device_uwb.h`

Device/initiator side of the UWB ranging-service setup codec: the interface for
parsing the reader's M1 and M3 and building the device's M2 and M4. Declares the
decoded views of M1 and M3, the parameter structs the two builders take, and
select_m2, which chooses a config and slot layout from what M1 offered. Pure
TLV, no crypto and no session state, so it is host-testable against the reader's
own codec by loopback.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](aliro_uwb_msg_spec.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](aliro_device_uwb.c.md)

## API

### `struct aliro_dev_uwb_m1`
`modules/woz_uwb/src/aliro/aliro_device_uwb.h:31`

Ranging capabilities the reader offered in M1 (reader -> device).

### `struct aliro_dev_uwb_m3`
`modules/woz_uwb/src/aliro/aliro_device_uwb.h:41`

Ranging parameters the reader committed in M3 (reader -> device).

### `struct aliro_dev_uwb_m2_params`
`modules/woz_uwb/src/aliro/aliro_device_uwb.h:52`

Device selections carried in M2 (device -> reader).

### `struct aliro_dev_uwb_m4_params`
`modules/woz_uwb/src/aliro/aliro_device_uwb.h:63`

Device selections carried in M4 (device -> reader).
