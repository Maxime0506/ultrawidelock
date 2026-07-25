<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/uwb_cirdiag.h`

@file uwb_cirdiag.h — Per-reception CIA first-path/STS diagnostics stream (channel-impulse
Stage 0). The RX callback latches the DW3000's CIA diagnostic bank (Ipatov/STS first-path
index, F1..F3, power, peak, STS quality, xtal offset); task context emits it as one
"[ALAB] t=<us> ev=uwb.diag ..." line for tools/aliro_lab.py. OFF at boot; armed at runtime
(nRF `aliro cir on`, ESP32 rides the `lab on` gate).

**used by** [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](../modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](../modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md)

<details><summary>Undocumented (9)</summary>

- `uwb_cirdiag_set_enabled`
- `uwb_cirdiag_enabled`
- `uwb_cirdiag_dump_set_enabled`
- `uwb_cirdiag_dump_enabled`
- `uwb_cirdiag_ring_count`
- `uwb_cirdiag_capture`
- `uwb_cirdiag_flush`
- `uwb_cirdiag_window_due`
- `uwb_cirdiag_probe`

</details>
