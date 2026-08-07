<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/driver/uwb_seam.h`

@file uwb_seam.h — the DW3000 entry points the CCC engine owns.
Four decadriver calls carry engine behaviour that no caller may skip: arming
RX must program the CCC STS for the slot, loading an STS-IV must substitute
the CCC STS-V, registering callbacks must insert the Pre-POLL shim, and
(re)configuring the PHY is traced. Every call site in this module goes
through the helpers below instead of <deca_device_api.h>, so a site added
later cannot quietly bypass any of it. scripts/check-uwb-seam.sh enforces
that mechanically.
Under CONFIG_WOZ_ALIRO the engine supplies the definitions:
woz_uwb_arm_rx         ccc_shim_rx.c    program the CCC key/IV, then arm RX
woz_uwb_set_sts_iv     ccc_shim_wrap.c  substitute the CCC STS-V per frame
woz_uwb_set_callbacks  uwb_rxdiag.c     insert the tally + Pre-POLL shims
woz_uwb_configure_phy  uwb_rxdiag.c     log the PHY configuration
The ESP32 port omits uwb_rxdiag.c (it is k_work-based) and supplies the last
two from port/woz_seam_stubs.c instead. Below the CONFIG_WOZ_ALIRO tier there
is no engine to reach, so each helper inlines to the plain decadriver call.
The implementations are free to call the decadriver directly — that is how
they reach the hardware, and how a site that has already programmed the STS
itself (the self-rearm paths in ccc_shim_rx.c) opts out on purpose.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](../modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](uwb_min.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](uwb_rxdiag.c.md)  ·  **discussed in** [`anchor/README.md`](../../../anchor/README.md), [`docs/porting-esp32.md`](../../porting-esp32.md), [`docs/porting.md`](../../porting.md), [`ports/esp32/apps/reader/README.md`](../../../ports/esp32/apps/reader/README.md), [`ports/esp32/components/woz_uwb/README.md`](../../../ports/esp32/components/woz_uwb/README.md)

## API

### `static inline int32_t woz_uwb_arm_rx(int32_t mode)`
`modules/woz_uwb/src/driver/uwb_seam.h:51`

@brief Enable RX. No CCC STS exists at this tier, so this is the bare call.

### `static inline void woz_uwb_set_sts_iv(dwt_sts_cp_iv_t *iv)`
`modules/woz_uwb/src/driver/uwb_seam.h:57`

@brief Load an STS-IV verbatim: there is no CCC STS-V to substitute here.

### `static inline void woz_uwb_set_callbacks(dwt_callbacks_s *callbacks)`
`modules/woz_uwb/src/driver/uwb_seam.h:63`

@brief Register callbacks with no shim in front of them.

### `static inline int32_t woz_uwb_configure_phy(dwt_config_t *config)`
`modules/woz_uwb/src/driver/uwb_seam.h:69`

@brief Apply a full PHY configuration, untraced.
