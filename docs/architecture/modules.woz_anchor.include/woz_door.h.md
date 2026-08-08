<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_door.h`

**used by** [`modules/woz_anchor/src/woz_door.c`](../modules.woz_anchor.src/woz_door.c.md)

## API

### `struct woz_door_cfg`
`modules/woz_anchor/include/woz_door.h:49`

Fixed geometry, measured once at install. See the mounting requirement above.

### `struct woz_door_thresholds`
`modules/woz_anchor/include/woz_door.h:79`

Hysteresis band, in millidegrees, plus the dwell.
Expressed in ANGLE rather than distance on purpose: re-hanging the door or
moving an anchor changes the distance that corresponds to "ajar" but not the
angle, so a geometry change is absorbed by recalibrating woz_door_cfg and
these stay put.

### `struct woz_door`
`modules/woz_anchor/include/woz_door.h:91`

Running state. Caller-owned; this module instantiates nothing.
