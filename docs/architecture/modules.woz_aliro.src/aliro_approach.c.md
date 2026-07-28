<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_approach.c`

@file aliro_approach.c
Kalman-filtered approach controller for predictive unlock. Tracks distance (cm), velocity (cm/s),
and estimated time-to-arrival (ms) at the unlock radius. Supervises presence via median filtering
of trusted ranges and fires predictive unlock when closing speed and ETA meet thresholds. Factory
defaults: unlock 100 cm, relock 250 cm, dwell times 2 s and 3 s, motor delay 500 ms, margin 250
ms, velocity floor 30 cm/s, prediction enabled.

**depends on** [`modules/woz_aliro/include/aliro_approach.h`](../modules.woz_aliro.include/aliro_approach.h.md)

## API

### `static void kf_rebase(struct aliro_approach *ap, int64_t now_ms, int32_t cm)`
`modules/woz_aliro/src/aliro_approach.c:41`

Reset the Kalman filter to a new state given a fresh measurement: clears rejection history,
zeroes velocity, reinitializes covariance, and resets prediction state.

**called by** `kf_update`

### `static bool kf_update(struct aliro_approach *ap, int64_t now_ms, int32_t cm)`
`modules/woz_aliro/src/aliro_approach.c:58`

Time update always (time really passed), measurement update only inside the
innovation gate. Returns true when the sample updated the estimate.

**called by** `aliro_approach_feed`  ·  **calls** `kf_rebase`

### `static int32_t range_median(const int32_t *win, int n)`
`modules/woz_aliro/src/aliro_approach.c:113`

Median of the first n samples of win (n in [1, ALIRO_APPROACH_MEDIAN_N]).
Rejects the metre-scale spikes in the per-block UWB distance without the
lag of a running average.

**called by** `aliro_approach_feed`

### `void aliro_approach_defaults(struct aliro_approach_cfg *cfg)`
`modules/woz_aliro/src/aliro_approach.c:138`

Initialize an approach configuration with factory defaults: unlock at 100 cm, relock at 250 cm,
dwell times 2 s and 3 s, motor delay 500 ms, margin 250 ms, velocity floor 30 cm/s, predictive
unlock enabled.

**called by** `aliro_approach_init`

### `void aliro_approach_init(struct aliro_approach *ap, const struct aliro_approach_cfg *cfg)`
`modules/woz_aliro/src/aliro_approach.c:154`

Initialize an approach controller to locked state with zero velocity and no prediction in flight.
If cfg is NULL, load factory defaults; otherwise copy the provided configuration.

**called by** `aliro_approach_gone`  ·  **calls** `aliro_approach_defaults`

### `static enum aliro_approach_action pred_abort(struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:170`

Record a predictive unlock abort and relock the door. Called when prediction is active but the
phone has stopped approaching or moved away.

**called by** `aliro_approach_feed`, `aliro_approach_tick`

### `enum aliro_approach_action aliro_approach_feed(struct aliro_approach *ap, int64_t now_ms, int32_t cm)`
`modules/woz_aliro/src/aliro_approach.c:185`

Update the Kalman filter state with a new range measurement, compute estimated time-to-arrival
(ETA) at the unlock radius, track presence via a median-filter window, and supervise predictive
unlock (fire early when closing speed and ETA permit, abort if the phone stops or moves away).
Return the action code: UNLOCK_THRESHOLD (entered unlock zone), RELOCK_DEPART (exited relock
zone), UNLOCK_PREDICT (fired a predictive unlock), or HOLD (no action).

**calls** `kf_update`, `pred_abort`, `range_median`

### `enum aliro_approach_action aliro_approach_tick(struct aliro_approach *ap, int64_t now_ms)`
`modules/woz_aliro/src/aliro_approach.c:267`

Supervise an active predictive unlock when no new measurement arrives this window. If the
prediction deadline has passed, abort and relock the door. Return HOLD otherwise.

**calls** `pred_abort`

### `enum aliro_approach_action aliro_approach_gone(struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:282`

Reset the approach controller to locked state while preserving its configuration. Return
RELOCK_DEPART if the door was unlocked before the reset, otherwise HOLD.

**calls** `aliro_approach_init`

### `bool aliro_approach_locked(const struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:294`

Return true if the door is locked, false if unlocked.

### `int32_t aliro_approach_est_cm(const struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:303`

Return the current estimated distance in centimeters. Returns -1 if the Kalman filter has not
been initialized (no valid measurement yet); otherwise returns the rounded estimate.

### `int32_t aliro_approach_vel_cm_s(const struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:315`

Return the current velocity in centimeters per second (positive = approaching, negative =
receding). Returns 0 if the Kalman filter has not been initialized.

### `int32_t aliro_approach_eta_ms(const struct aliro_approach *ap)`
`modules/woz_aliro/src/aliro_approach.c:329`

Return the estimated time in milliseconds until approach completes (unlock reaches the door).
Value is -1 if not yet computed, or the reader has already locked the door.
