<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_approach.c`

*No module docstring. First commit: "approach: predict time-of-arrival, open the bolt at arrival".*

**depends on** [`modules/woz_aliro/include/aliro_approach.h`](../modules.woz_aliro.include/aliro_approach.h.md)

## API

### `static bool kf_update(struct aliro_approach *ap, int64_t now_ms, int32_t cm)`
`modules/woz_aliro/src/aliro_approach.c:46`

Time update always (time really passed), measurement update only inside the
innovation gate. Returns true when the sample updated the estimate.

**called by** `aliro_approach_feed`  ·  **calls** `kf_rebase`

### `static int32_t range_median(const int32_t *win, int n)`
`modules/woz_aliro/src/aliro_approach.c:101`

Median of the first n samples of win (n in [1, ALIRO_APPROACH_MEDIAN_N]).
Rejects the metre-scale spikes in the per-block UWB distance without the
lag of a running average.

**called by** `aliro_approach_feed`

<details><summary>Undocumented (11)</summary>

- `kf_rebase`
- `aliro_approach_defaults` — tested: approach
- `aliro_approach_init`
- `pred_abort`
- `aliro_approach_feed` — tested: approach
- `aliro_approach_tick` — tested: approach
- `aliro_approach_gone` — tested: approach
- `aliro_approach_locked` — tested: approach
- `aliro_approach_est_cm` — tested: approach
- `aliro_approach_vel_cm_s` — tested: approach
- `aliro_approach_eta_ms` — tested: approach

</details>
