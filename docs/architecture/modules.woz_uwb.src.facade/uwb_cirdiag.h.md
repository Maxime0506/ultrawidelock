<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/uwb_cirdiag.h`

@file uwb_cirdiag.h — Per-reception CIA first-path/STS diagnostics stream (channel-impulse
Stage 0). The RX callback latches the DW3000's CIA diagnostic bank (Ipatov/STS first-path
index, F1..F3, power, peak, STS quality, xtal offset); task context emits it as one
"[ALAB] t=<us> ev=uwb.diag ..." line for tools/aliro_lab.py. OFF at boot; armed at runtime
(nRF `aliro cir on`, ESP32 rides the `lab on` gate).

**used by** [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](../modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](../modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md)

## API

### `struct uwb_cirdiag_ipatov`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:33`

@brief The Ipatov scalars of the latest latched reception, for a classifier.
Field for field from dwt_rxdiag_t, and deliberately NOT struct woz_ml_cia even
though the two are the same five numbers: woz_uwb is the lower layer and must
not acquire a dependency on woz_ml to hand out registers it already holds. The
caller copies across by name, which is checkable by eye — see
firmware/src/main.c, and see woz_ml.h on why five same-typed integers are
passed as a struct rather than positionally.

### `static inline void uwb_cirdiag_set_enabled(bool on)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:144`

Stub: set_enabled is a no-op.

### `static inline bool uwb_cirdiag_enabled(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:151`

Stub: enabled returns false.

### `static inline void uwb_cirdiag_dump_set_enabled(bool on)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:158`

Stub: dump_set_enabled is a no-op.

### `static inline bool uwb_cirdiag_dump_enabled(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:165`

Stub: dump_enabled returns false.

### `static inline uint32_t uwb_cirdiag_ring_count(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:172`

Stub: ring_count returns 0.

### `static inline bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending, bool is_final)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:179`

Stub: capture returns false.

### `static inline void uwb_cirdiag_flush(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:191`

Stub: flush is a no-op.

### `static inline bool uwb_cirdiag_window_due(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:197`

Stub: window_due returns false.

### `static inline void uwb_cirdiag_probe(void)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:204`

Stub: probe is a no-op.

### `static inline bool uwb_cirdiag_last_ipatov(struct uwb_cirdiag_ipatov *out)`
`modules/woz_uwb/src/facade/uwb_cirdiag.h:210`

Stub: last_ipatov returns false and writes nothing.
