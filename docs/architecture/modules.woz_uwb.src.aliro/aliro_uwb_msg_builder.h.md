<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`

@file aliro_uwb_msg_builder.h — big-endian TLV message builder.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](aliro_device_uwb.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](aliro_uwb_msg_builder.c.md)

## API

### `struct aliro_uwb_msg_builder`
`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h:19`

@brief Accumulates bytes into a heap-allocated Aliro UWB message.
@param message Aliro UWB message under construction, holding encoded M1-M4 attributes and
payload.
@param capacity Capacity of the message buffer.
