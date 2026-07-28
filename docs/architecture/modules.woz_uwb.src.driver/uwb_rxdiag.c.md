<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/driver/uwb_rxdiag.c`

@file uwb_rxdiag.c — Diagnostic RX/TX event tallies + ranging heartbeat.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](../modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](../modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](../modules.woz_uwb.src.facade/woz_diag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md)  ·  **discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md), [`docs/porting.md`](../../porting.md)

## API

### `static void cad_mark(void)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:57`

@brief Bin one RX detection's phase within the 192 ms block grid.

**called by** `shim_rxerr`, `shim_rxok`

### `static void rxdiag_ev_log(const char *cls, const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:69`

@brief Log one RX event's frame structure until the budget is spent.

**called by** `shim_rxerr`, `shim_rxok`, `shim_rxto`

### `static void cirdiag_emit(struct k_work *work)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:86`

@brief Task-side emitter for the latched CIA diagnostics (uwb_cirdiag).

### `static void shim_rxok(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:98`

@brief RX-good callback shim: log RX diagnostics, invoke the armed CCC callback, then decode the
Pre-POLL frame off the critical path.
@param d DW3000 RX callback data (may be NULL on POLL event).

**calls** `cad_mark`, `rxdiag_ev_log`

### `static void shim_rxto(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:148`

@brief RX-timeout shim: tally, then run the blob's handler.

**calls** `rxdiag_ev_log`

### `static void shim_rxerr(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:158`

@brief RX-error shim: tally + latch status (STS/CIA bits), then chain.

**calls** `cad_mark`, `rxdiag_ev_log`

### `static void shim_txdone(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:172`

@brief TX-done shim: tally, then run the blob's handler.

### `void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:181`

@brief Intercept the callback registration and insert counting shims.

### `int32_t __wrap_dwt_configure(dwt_config_t *config)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:209`

@brief Log every full PHY configuration the blob issues.

### `void __wrap_dwt_configurestsmode(uint8_t stsMode)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:226`

@brief Log every STS-mode (CP_SPC) write the blob issues, then pass through.

### `static void rxdiag_log(struct k_work *work)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:242`

@brief Periodic ranging heartbeat (every 2 s); re-arms itself while streaming.

### `void uwb_rxdiag_get_counts(uint32_t *rxok, uint32_t *rxerr, uint32_t *rxto, uint32_t *txdone, uint32_t *last_err, uint32_t *last_ok)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:307`

@brief Snapshot the running RX/TX event tallies; out-params optional (NULL to skip).

### `void uwb_rxdiag_stream_set(bool on)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:330`

@brief Arm or cancel the periodic ranging heartbeat (backs `aliro log on|off`).

### `bool uwb_rxdiag_stream_get(void)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:340`

@brief Whether the periodic ranging heartbeat is currently armed.

### `void uwb_rxdiag_rng_set(bool on)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:345`

@brief Arm or cancel the per-block distance stream (backs `aliro frames on|off`).

### `bool uwb_rxdiag_rng_get(void)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:350`

@brief Whether the per-block distance stream is currently armed.

### `static int rxdiag_init(void)`
`modules/woz_uwb/src/driver/uwb_rxdiag.c:356`

@brief Arm the periodic heartbeat at application init.
