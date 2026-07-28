<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_lat.h`

@file aliro_lat.h
Latency tracking for Aliro protocol phases during a walk-up: record BLE_CONNECT as epoch zero,
mark timestamps for each phase, emit a report with elapsed intervals and flight-recorder
diagnostics.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](../modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `static inline void aliro_lat_begin(void)`
`modules/woz_aliro/include/aliro_lat.h:74`

Initialize latency tracking; record the BLE_CONNECT epoch as the zero reference for all
subsequent phase timestamps.

### `static inline int aliro_lat_mark(enum aliro_lat_phase phase)`
`modules/woz_aliro/include/aliro_lat.h:81`

Record the current uptime for a given Aliro protocol phase if not yet marked; return 1 if newly
recorded, 0 if already marked or out of range.

### `static inline void aliro_lat_report(void)`
`modules/woz_aliro/include/aliro_lat.h:90`

Emit a latency report showing all recorded phase timestamps and elapsed intervals; dump
flight-recorder lab traces if CONFIG_WOZ_ALIRO_LAB is enabled and tracing is active.
