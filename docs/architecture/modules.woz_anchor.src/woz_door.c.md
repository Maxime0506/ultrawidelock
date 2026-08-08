<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/src/woz_door.c`

**depends on** [`modules/woz_anchor/include/woz_door.h`](../modules.woz_anchor.include/woz_door.h.md)

## API

### `static int32_t cos_q15(int32_t mddeg)`
`modules/woz_anchor/src/woz_door.c:42`

@brief cos of @p mddeg (clamped to 0..180 degrees), Q15.

**called by** `sin_q15`, `woz_door_resolution_mddeg`

### `static int32_t sin_q15(int32_t mddeg)`
`modules/woz_anchor/src/woz_door.c:61`

@brief sin of @p mddeg via the same table, Q15. Valid for 0..180 degrees.

**called by** `woz_door_resolution_mddeg`  ·  **calls** `cos_q15`

### `static int32_t acos_mddeg(int32_t q15)`
`modules/woz_anchor/src/woz_door.c:73`

@brief Arccos of a Q15 value, in millidegrees.
Binary search over the table, which is monotone decreasing, then linear
interpolation inside the bracketing degree.

**called by** `woz_door_angle_mddeg`

### `static bool cfg_ok(const struct woz_door_cfg *cfg)`
`modules/woz_anchor/src/woz_door.c:114`

@brief Is this geometry capable of producing an angle at all?

**called by** `woz_door_angle_mddeg`, `woz_door_init`, `woz_door_resolution_mddeg`

### `static enum woz_door_state classify(const struct woz_door *d, int32_t mddeg)`
`modules/woz_anchor/src/woz_door.c:234`

@brief Which state one angle argues for, given where we already are.
The hysteresis lives here: leaving a state needs a bigger move than entering
it did, so a door resting on a threshold does not oscillate.

**called by** `woz_door_feed`

<details><summary>Undocumented (5)</summary>

- `woz_door_defaults`
- `woz_door_angle_mddeg`
- `woz_door_resolution_mddeg`
- `woz_door_init`
- `woz_door_feed`

</details>
