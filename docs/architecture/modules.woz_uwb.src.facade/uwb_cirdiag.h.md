<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/uwb_cirdiag.h`

@file uwb_cirdiag.h — Per-reception CIA first-path/STS diagnostics stream (channel-impulse
Stage 0). The RX callback latches the DW3000's CIA diagnostic bank (Ipatov/STS first-path
index, F1..F3, power, peak, STS quality, xtal offset); task context emits it as one
"[ALAB] t=<us> ev=uwb.diag ..." line for tools/aliro_lab.py. OFF at boot; armed at runtime
(nRF `aliro cir on`, ESP32 rides the `lab on` gate).

**used by** [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](../modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](../modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md)

## API

### `static inline void uwb_cirdiag_set_enabled(bool on)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:95`

Stub: set_enabled is a no-op.

### `static inline bool uwb_cirdiag_enabled(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:102`

Stub: enabled returns false.

### `static inline void uwb_cirdiag_dump_set_enabled(bool on)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:109`

Stub: dump_set_enabled is a no-op.

### `static inline bool uwb_cirdiag_dump_enabled(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:116`

Stub: dump_enabled returns false.

### `static inline uint32_t uwb_cirdiag_ring_count(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:123`

Stub: ring_count returns 0.

### `static inline bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:130`

Stub: capture returns false.

### `static inline void uwb_cirdiag_flush(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:140`

Stub: flush is a no-op.

### `static inline bool uwb_cirdiag_window_due(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:146`

Stub: window_due returns false.

### `static inline void uwb_cirdiag_probe(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:153`

Stub: probe is a no-op.
