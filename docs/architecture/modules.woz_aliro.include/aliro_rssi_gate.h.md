<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_rssi_gate.h`

BLE-RSSI ranging power gate: decides when the phone is close enough that arming
UWB ranging is worth the radio's RX power. Pure sample-in/state-out logic (EWMA
smoothing, open/close hysteresis with a close hold-off, optional rise-rate fast
open for fast approaches) so it host-tests without a radio; the reader feeds it
connection RSSI samples and defers Reader-Status-AP-Completed until it opens.

**used by** [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_rssi_gate.c`](../modules.woz_aliro.src/aliro_rssi_gate.c.md)

## API

### `struct aliro_rssi_gate_cfg`
`modules/woz_aliro/include/aliro_rssi_gate.h:21`

Tuning knobs. Kconfig supplies the build defaults (ALIRO_RSSI_GATE_CFG_DEFAULT).
The thresholds come from a measured curve on one rig (docs/power-profile.md);
they are per-room and per-handset, so re-run the calibration for a new one.

### `struct aliro_rssi_gate`
`modules/woz_aliro/include/aliro_rssi_gate.h:64`

Gate state. All-zeroes == reset/closed, so a zeroed session slot needs no init
call. Internal values are Q4 fixed point (dBm * 16).
