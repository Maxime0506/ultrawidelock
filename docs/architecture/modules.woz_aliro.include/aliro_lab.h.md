<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_lab.h`

Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
parsed by tools/aliro_lab.py into a scored walk-up report. Ships in every Aliro
build (CONFIG_WOZ_ALIRO_LAB defaults y, like the sibling uwbdiag trace) but is
OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
any firmware profiles on demand with no reflash. Set CONFIG_WOZ_ALIRO_LAB=n to
strip it from a hardened production image.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](../modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `static inline void aliro_lab_set_enabled(bool on)`
`modules/woz_aliro/include/aliro_lab.h:63`

Enable or disable Aliro Lab instrumentation. When disabled, all recording functions become
no-ops. Caller must invoke before any approach transactions if instrumentation is desired.

### `static inline bool aliro_lab_enabled(void)`
`modules/woz_aliro/include/aliro_lab.h:72`

Return whether Aliro Lab instrumentation is active. Returns false when CONFIG_WOZ_ALIRO_LAB is
not enabled.

### `static inline void aliro_lab_ev(const char *ev)`
`modules/woz_aliro/include/aliro_lab.h:81`

Record a named event for Aliro Lab when enabled; no-op stub when disabled. Caller passes a unique
event name; the call is recorded with a timestamp.

### `static inline void aliro_lab_evi(const char *ev, const char *key, long val)`
`modules/woz_aliro/include/aliro_lab.h:91`

Record a named event with one integer sample for Aliro Lab when enabled; no-op stub when
disabled. Caller passes event name, key (e.g., "distance_cm"), and value; the triple is recorded
with a timestamp.

### `static inline void aliro_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2)`
`modules/woz_aliro/include/aliro_lab.h:102`

Record a named event with two integer samples for Aliro Lab when enabled; no-op stub when
disabled. Caller passes event name, two key-value pairs; both are recorded with a timestamp.

### `static inline void aliro_lab_dump(void)`
`modules/woz_aliro/include/aliro_lab.h:114`

No-op stub when CONFIG_WOZ_ALIRO_LAB is not enabled. Caller may invoke any time; does nothing.
