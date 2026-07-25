<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_lat.c`

Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.

**depends on** [`modules/woz_aliro/include/aliro_lab.h`](../modules.woz_aliro.include/aliro_lab.h.md), [`modules/woz_aliro/include/aliro_lat.h`](../modules.woz_aliro.include/aliro_lat.h.md), [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md)

## API

### `void aliro_lat_begin(void)`
`modules/woz_aliro/src/aliro_lat.c:44`

Initialize latency tracking and record the BLE connection epoch; reset all phase timestamps to
zero and clear the lab-dump flag if CONFIG_WOZ_ALIRO_LAB is enabled.

### `int aliro_lat_mark(enum aliro_lat_phase phase)`
`modules/woz_aliro/src/aliro_lat.c:57`

Record the current uptime for a given Aliro protocol phase if not yet marked; return 1 if newly
recorded, 0 if already marked or phase index is out of range.

### `void aliro_lat_report(void)`
`modules/woz_aliro/src/aliro_lat.c:71`

Print a one-line latency summary to stdout showing each Aliro phase as milliseconds offset from
BLE connect, or "-" if the phase was never reached; also emit any flight-recorder lab traces if
enabled.

**calls** `aliro_lab_dump`

### `void aliro_lab_set_enabled(bool on)`
`modules/woz_aliro/src/aliro_lat.c:100`

Enable or disable Aliro latency lab tracing (CONFIG_WOZ_ALIRO_LAB flight-recorder output).

### `bool aliro_lab_enabled(void)`
`modules/woz_aliro/src/aliro_lat.c:108`

Return true if Aliro latency lab tracing (CONFIG_WOZ_ALIRO_LAB) is enabled; false otherwise.

### `void aliro_lab_ev(const char *ev)`
`modules/woz_aliro/src/aliro_lat.c:117`

Log a timestamped flight-recorder event with description ev to stdout if lab tracing is enabled;
no-op if disabled.

### `void aliro_lab_evi(const char *ev, const char *key, long val)`
`modules/woz_aliro/src/aliro_lat.c:129`

Log a timestamped flight-recorder event with description ev and one key=value pair to stdout if
lab tracing is enabled; no-op if disabled.

### `void aliro_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2)`
`modules/woz_aliro/src/aliro_lat.c:141`

Log a timestamped flight-recorder event with description ev and two key=value pairs to stdout if
lab tracing is enabled; no-op if disabled.

### `void aliro_lab_dump(void)`
`modules/woz_aliro/src/aliro_lat.c:154`

Emit flight-recorder lab traces for every recorded Aliro latency phase to stdout in [ALAB] format
if lab tracing is enabled and has not yet been dumped.

**called by** `aliro_lat_report`
