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

### `class Event`
`tools/aliro_lab.py:105`

A timestamped firmware trace event: monotonic microsecond offset from capture start, event name, attributes dictionary, and source line number in the firmware log.
Constructed by the parser; used to build transactions and compute timing deltas.

**called by** `parse_events`

#### `Event.__init__(self, t_us, name, attrs, line_no)`
`tools/aliro_lab.py:109`

Initialize an Event with its timestamp in microseconds, symbolic name, attribute dictionary (typically containing proto/id/len/payload keys), and line number in the source trace.

### `class Transaction`
`tools/aliro_lab.py:117`

One walk-up: the events between a session.start and its session.end.

**called by** `split_transactions`

#### `Transaction.__init__(self, index)`
`tools/aliro_lab.py:120`

Initialize a Transaction with a unique walk-up index, an empty event list, and the open flag set (no session.end seen yet).

#### `Transaction.finish(self)`
`tools/aliro_lab.py:126`

Sort events by timestamp, extract phase markers (ph.* names, first occurrence only, tracking duplicates), infer flow type (fast or standard) from flow.* markers, and collect trusted range samples (cm values from range events). Skip reason "incomplete" set until flow type is determined.

**called by** `split_transactions`  ·  **calls** `Transaction.has`

#### `Transaction.has(self, name)`
`tools/aliro_lab.py:148`

Return true if the transaction contains at least one event with the given symbolic name; false otherwise.
Used to check for required or optional milestones in the walk-up sequence.

**called by** `Transaction.finish`, `run_checks`

#### `Transaction.first(self, name)`
`tools/aliro_lab.py:154`

Return the first event in this transaction matching the given symbolic name, or None if no such event exists.
Used to locate anchor events like session.start or connect for timestamp calculations.

**called by** `Transaction.t0`

#### `Transaction.named(self, name)`
`tools/aliro_lab.py:163`

Return a list of all events in this transaction with the given symbolic name.

**called by** `render_approach_svg`, `run_checks`

#### `Transaction.t0(self)`
`tools/aliro_lab.py:167`

Walk-up zero: the connect stamp, else the session.start line.

**called by** `Transaction.offset_ms`, `render_approach_svg`, `render_html`, `render_terminal`, `run_checks`  ·  **calls** `Transaction.first`

#### `Transaction.offset_ms(self, key)`
`tools/aliro_lab.py:174`

Compute the millisecond offset from the walk-up zero (connect or session start) to a given phase key. Divide microsecond delta by 1000.

**called by** `run_checks`  ·  **calls** `Transaction.t0`

#### `Transaction.last_phase(self)`
`tools/aliro_lab.py:178`

Return the key of the last phase (in PHASES order) that was observed in this transaction, or None if no phases are present.

**called by** `run_checks`

### `parse_events(text)`
`tools/aliro_lab.py:187`

Parse firmware log text line-by-line, extracting Aliro Lab events via regex (timestamp, event name, attribute key=value pairs). Return a list of Event objects sorted by line number.

**called by** `main`  ·  **calls** `Event`

### `cir_rows(events)`
`tools/aliro_lab.py:199`

Windowed-CIR taps as (t_us, n, i, re, im, mag2), in capture order. One
row per ev=uwb.cir line with the full i/re/im set; magnitude-squared is
precomputed for convenience. Works on the raw event stream, so taps are
kept even outside a walk-up session (idle-ranging captures).

**called by** `write_cir_csv`

### `write_cir_csv(events, path)`
`tools/aliro_lab.py:216`

Write the CIR taps to a CSV; return the row count.

**called by** `main`  ·  **calls** `cir_rows`

### `split_transactions(events)`
`tools/aliro_lab.py:226`

Group by session boundaries in LINE order (the ph.* dump lines carry
historical timestamps, so t order can't delimit walk-ups).

**called by** `main`  ·  **calls** `Transaction`, `Transaction.finish`

### `run_checks(txn)`
`tools/aliro_lab.py:247`

Invariant checks; each returns (id, class, status, detail) with class
the worst it can score (fail/warn) and status pass/warn/fail/n-a.

**called by** `main`  ·  **calls** `Transaction.has`, `Transaction.last_phase`, `Transaction.named`, `Transaction.offset_ms`, `Transaction.t0`, `add`

### `add(cid, cls, ok, detail, applicable=True)`
`tools/aliro_lab.py:252`

Append a check result (id, class, status, detail) to results. Status is "n/a" if not applicable, "pass" if ok is true, otherwise the class string (warn or fail).

**called by** `run_checks`

### `worst_status(all_checks)`
`tools/aliro_lab.py:380`

Return the worst (most critical) status among all checks across all transactions: "fail" if any check failed, else "warn" if any warned, else "pass".

**called by** `main`, `render_html`, `render_terminal`

### `fmt_ms(us_delta)`
`tools/aliro_lab.py:390`

Format a microsecond delta as milliseconds (one decimal place).

**called by** `render_html`, `render_terminal`

### `render_terminal(name, txns, checks_by_txn, use_color)`
`tools/aliro_lab.py:397`

Render transaction summary, phase timeline, approach ranges, and check results as colored terminal output (ANSI colors if stdout is a TTY).

**called by** `main`  ·  **calls** `Transaction.t0`, `fmt_ms`, `paint`, `sdu_label`, `worst_status`

### `paint(status, text)`
`tools/aliro_lab.py:399`

Wrap text in ANSI color code if use_color is true, otherwise return unchanged. Codes: "32" for green, "33" for yellow, "31" for red.

**called by** `render_terminal`

### `render_approach_svg(txn)`
`tools/aliro_lab.py:538`

Distance-over-time chart of the approach: one dot per trusted range,
dashed markers at grant/bolt/relock. Inline SVG, themed via the CSS vars.

**called by** `render_html`  ·  **calls** `Transaction.named`, `Transaction.t0`, `sx`, `sy`

### `sx(x)`
`tools/aliro_lab.py:564`

Compute the screen x-coordinate (pixels) for a data value x in the range [xmin, xmax], scaling to the plot width minus left/right padding.

**called by** `render_approach_svg`

### `sy(y)`
`tools/aliro_lab.py:568`

Map a y-coordinate to SVG pixel space, scaling from data range [ymin, ymax] into the chart's vertical span minus padding.

**called by** `render_approach_svg`

### `render_html(name, txns, checks_by_txn)`
`tools/aliro_lab.py:600`

Render a transaction log and check results as a self-contained HTML document with styling, phase timeline, ranging approach chart, and tabular check details.

**called by** `main`  ·  **calls** `Transaction.t0`, `fmt_ms`, `render_approach_svg`, `sdu_label`, `worst_status`

### `main(argv)`
`tools/aliro_lab.py:690`

Parse command-line arguments, read firmware trace log, extract walk-up transactions and run checks, optionally export CIR tap data to CSV, emit terminal and HTML reports, return exit code based on check results.

**calls** `parse_events`, `render_html`, `render_terminal`, `run_checks`, `split_transactions`, `worst_status`, `write_cir_csv`
