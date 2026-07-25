<!-- generated documentation — edit the source, not this file -->
# `tools/aliro_lab.py`

Aliro Lab: score a captured reader serial log.

Usage: python3 tools/aliro_lab.py <capture.log> [report.html]

Parses the structured "[ALAB] t=<us> ev=..." trace lines the firmware emits
when CONFIG_WOZ_ALIRO_LAB is enabled (see modules/woz_aliro/src/aliro_lab.h),
groups them into walk-up transactions, and reports phase timings, the flow
taken (fast vs standard), and pass/warn/fail invariant checks — to the
terminal and as a self-contained HTML report (default: <capture.log>.html).

Every check encodes an invariant of this repo's reader implementation (see
internal notes in the check text), nothing else. Exit status: 0 = no failing
check, 1 = at least one FAIL, 2 = usage/input error.

**used by** [`tools/aliro_gait.py`](aliro_gait.md)  ·  **discussed in** [`docs/hardware-validation.md`](../../hardware-validation.md)

## API

### `sdu_label(ev)`
`tools/aliro_lab.py:74`

Human name for an rtx/rrx event, e.g. "rtx M1".

Captures from firmware before the id-based latency stamping carry no
`proto`, and there the id alone is ambiguous; those render as "rrx id=1".

**called by** `render_html`, `render_terminal`

### `class Transaction`
`tools/aliro_lab.py:109`

One walk-up: the events between a session.start and its session.end.

**called by** `split_transactions`

#### `Transaction.t0(self)`
`tools/aliro_lab.py:150`

Walk-up zero: the connect stamp, else the session.start line.

**called by** `Transaction.offset_ms`, `render_approach_svg`, `render_html`, `render_terminal`, `run_checks`  ·  **calls** `Transaction.first`

### `split_transactions(events)`
`tools/aliro_lab.py:179`

Group by session boundaries in LINE order (the ph.* dump lines carry
historical timestamps, so t order can't delimit walk-ups).

**called by** `main`  ·  **calls** `Transaction`, `Transaction.finish`

### `run_checks(txn)`
`tools/aliro_lab.py:200`

Invariant checks; each returns (id, class, status, detail) with class
the worst it can score (fail/warn) and status pass/warn/fail/n-a.

**called by** `main`  ·  **calls** `Transaction.has`, `Transaction.last_phase`, `Transaction.named`, `Transaction.offset_ms`, `Transaction.t0`, `add`

### `render_approach_svg(txn)`
`tools/aliro_lab.py:486`

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
- `parse_events`
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
