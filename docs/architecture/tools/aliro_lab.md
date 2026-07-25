<!-- generated documentation — edit the source, not this file -->
# `tools/aliro_lab.py`

Aliro Lab: score a captured reader serial log.

Usage: python3 tools/aliro_lab.py [--cir <taps.csv>] <capture.log> [report.html]

Parses the structured "[ALAB] t=<us> ev=..." trace lines the firmware emits
when CONFIG_WOZ_ALIRO_LAB is enabled (see modules/woz_aliro/src/aliro_lab.h),
groups them into walk-up transactions, and reports phase timings, the flow
taken (fast vs standard), and pass/warn/fail invariant checks — to the
terminal and as a self-contained HTML report (default: <capture.log>.html).

With --cir, the windowed-CIR taps (ev=uwb.cir, channel-impulse Stage 1) are
also written to a CSV (t_us,n,i,re,im,mag2) for offline inside/outside
labeling and analysis; the scoring/report output is unchanged.

Every check encodes an invariant of this repo's reader implementation (see
internal notes in the check text), nothing else. Exit status: 0 = no failing
check, 1 = at least one FAIL, 2 = usage/input error.

**used by** [`tools/aliro_gait.py`](aliro_gait.md)  ·  **discussed in** [`docs/hardware-validation.md`](../../hardware-validation.md)

## API

### `sdu_label(ev)`
`tools/aliro_lab.py:78`

Human name for an rtx/rrx event, e.g. "rtx M1".

Captures from firmware before the id-based latency stamping carry no
`proto`, and there the id alone is ambiguous; those render as "rrx id=1".

**called by** `render_html`, `render_terminal`

### `class Transaction`
`tools/aliro_lab.py:113`

One walk-up: the events between a session.start and its session.end.

**called by** `split_transactions`

#### `Transaction.t0(self)`
`tools/aliro_lab.py:154`

Walk-up zero: the connect stamp, else the session.start line.

**called by** `Transaction.offset_ms`, `render_approach_svg`, `render_html`, `render_terminal`, `run_checks`  ·  **calls** `Transaction.first`

### `cir_rows(events)`
`tools/aliro_lab.py:183`

Windowed-CIR taps as (t_us, n, i, re, im, mag2), in capture order. One
row per ev=uwb.cir line with the full i/re/im set; magnitude-squared is
precomputed for convenience. Works on the raw event stream, so taps are
kept even outside a walk-up session (idle-ranging captures).

**called by** `write_cir_csv`

### `write_cir_csv(events, path)`
`tools/aliro_lab.py:200`

Write the CIR taps to a CSV; return the row count.

**called by** `main`  ·  **calls** `cir_rows`

### `split_transactions(events)`
`tools/aliro_lab.py:210`

Group by session boundaries in LINE order (the ph.* dump lines carry
historical timestamps, so t order can't delimit walk-ups).

**called by** `main`  ·  **calls** `Transaction`, `Transaction.finish`

### `run_checks(txn)`
`tools/aliro_lab.py:231`

Invariant checks; each returns (id, class, status, detail) with class
the worst it can score (fail/warn) and status pass/warn/fail/n-a.

**called by** `main`  ·  **calls** `Transaction.has`, `Transaction.last_phase`, `Transaction.named`, `Transaction.offset_ms`, `Transaction.t0`, `add`

### `render_approach_svg(txn)`
`tools/aliro_lab.py:517`

Distance-over-time chart of the approach: one dot per trusted range,
dashed markers at grant/bolt/relock. Inline SVG, themed via the CSS vars.

**called by** `render_html`  ·  **calls** `Transaction.named`, `Transaction.t0`, `sx`, `sy`

<details><summary>Undocumented (19)</summary>

- `Event` — tested: sdu label falls back without proto; sdu label named when proto present; sdu label names protocol when id unknown; sdu label unknown pair
- `Event.__init__`
- `Transaction.__init__`
- `Transaction.finish`
- `Transaction.has`
- `Transaction.first`
- `Transaction.named`
- `Transaction.offset_ms` — tested: phase offsets
- `Transaction.last_phase` — tested: incomplete walkup
- `parse_events` — tested: incomplete tap line skipped; rows parsed with mag2; write cir csv roundtrip
- `add`
- `worst_status` — tested: no failing check; no warn no fail; noise only
- `fmt_ms`
- `render_terminal`
- `paint`
- `sx`
- `sy`
- `render_html`
- `main`

</details>
