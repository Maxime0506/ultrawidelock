<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_mac.h`

@file ccc_mac.h — CCC UWB MAC layer: ranging-round scheduling, SP0 frame codec, DS-TWR.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](ccc_kdf.h.md), [`modules/woz_uwb/src/fira/ds_twr.h`](../modules.woz_uwb.src.fira/ds_twr.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_mac.c`](ccc_mac.c.md), [`modules/woz_uwb/src/ccc/ccc_session.h`](ccc_session.h.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](ccc_shim_rx.c.md)

## API

### `struct ccc_mhr_fields`
`modules/woz_uwb/src/ccc/ccc_mac.h:36`

@brief The per-frame-variable fields of an SP0 MHR (fixed fields are built in by ccc_build_mhr).

### `struct ccc_pre_poll`
`modules/woz_uwb/src/ccc/ccc_mac.h:53`

@brief Pre-POLL request message parameters.

### `struct ccc_responder_ts`
`modules/woz_uwb/src/ccc/ccc_mac.h:70`

@brief One responder's timestamp record in a Final_Data message.

### `struct ccc_final_data`
`modules/woz_uwb/src/ccc/ccc_mac.h:80`

@brief Final_Data message parameters.

### `struct ccc_ran_params`
`modules/woz_uwb/src/ccc/ccc_mac.h:117`

@brief Per-session ranging schedule parameters (negotiated in setup).

### `struct ccc_hop_decision`
`modules/woz_uwb/src/ccc/ccc_mac.h:129`

@brief The initiator's next-block hop decision, carried in Final_Data.
