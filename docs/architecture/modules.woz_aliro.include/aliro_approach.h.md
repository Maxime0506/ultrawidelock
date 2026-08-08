<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_approach.h`

@file aliro_approach.h
Configuration and state for approach detection and predictive unlock: unlock/relock thresholds in
centimeters, sample-count dwell times, motor retraction time, scheduling margin, minimum closing
speed, and a flag to enable or disable predictive ToA unlock.

**used by** [`modules/woz_aliro/src/aliro_approach.c`](../modules.woz_aliro.src/aliro_approach.c.md)

## API

### `struct aliro_approach_cfg`
`modules/woz_aliro/include/aliro_approach.h:76`

Configuration for approach detection: unlock_cm (presence radius and ETA target), relock_cm
(departure threshold), near_dwell/far_dwell (sample counts to unlock/relock), motor_ms (bolt
retraction time), margin_ms (scheduling slack, >= 192 ms to avoid missing discrete samples),
vmin_cm_s (min closing speed to arm prediction), predict_en (false disables prediction and leaves
presence path unchanged; also false whenever RSSI power gate is active).

### `struct aliro_approach`
`modules/woz_aliro/include/aliro_approach.h:232`

State machine and Kalman filter for approach detection and predictive unlock.
locked: bolt mirror state. win/wlen/wpos: median filter for distance. near_dwell/far_dwell:
hysteresis counters. kf_init/accepted/rejects/last_ms/d/v/p00/p01/p11: constant-velocity Kalman
filter (distance cm, velocity cm/s; negative velocity = closing).
pred_dwell/pred_open/pred_deadline_ms/eta_ms: predictive unlock path (fires when closing speed >=
vmin_cm_s and ETA to unlock_cm is within motor_ms + margin_ms).
