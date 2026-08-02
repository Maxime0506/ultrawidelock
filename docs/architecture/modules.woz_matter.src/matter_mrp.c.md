<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_mrp.c`

@file matter_mrp.c — MRP backoff schedule, retransmit state, replay window.

**depends on** [`modules/woz_matter/include/matter_mrp.h`](../modules.woz_matter.include/matter_mrp.h.md)

## API

### `static int32_t elapsed(uint32_t now, uint32_t since)`
`modules/woz_matter/src/matter_mrp.c:45`

Milliseconds from `since` to `now`, correct across the uint32 wrap.

**called by** `matter_mrp_next_deadline`, `reached`

### `static bool reached(uint32_t now, uint32_t deadline)`
`modules/woz_matter/src/matter_mrp.c:51`

True when `now` has reached or passed `deadline`.

**called by** `matter_mrp_poll`  ·  **calls** `elapsed`

<details><summary>Undocumented (11)</summary>

- `matter_mrp_backoff_ms` — tested: matter mrp
- `matter_mrp_window_init` — tested: matter mrp
- `matter_mrp_window_check` — tested: matter mrp
- `matter_mrp_window_commit` — tested: matter mrp
- `matter_mrp_init` — tested: matter mrp
- `matter_mrp_arm` — tested: matter mrp
- `matter_mrp_on_ack` — tested: matter mrp
- `matter_mrp_on_reliable_recv` — tested: matter mrp
- `matter_mrp_take_ack` — tested: matter mrp
- `matter_mrp_poll` — tested: matter mrp
- `matter_mrp_next_deadline` — tested: matter mrp

</details>
