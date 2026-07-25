<!-- generated documentation — edit the source, not this file -->
# `tools/power_profile.py`

Power profile: turn a gated-walk-up serial log (+ optional power capture)
into the mA / unlock-latency / approach numbers of the RSSI-gate study.

Usage: python3 tools/power_profile.py <capture.log> [--ppk trace.csv]
                                      [--tag LABEL] [--shift SECONDS] [--csv out.csv]
       python3 tools/power_profile.py <capture.log> --calibrate
                                      [--near-cm CM] [--pair-ms MS]

--calibrate answers a different question from the same captures: what the BLE
level actually means in metres on THIS reader in THIS room. Every walk-up already
interleaves `range cm=` (UWB ground truth) with `rssi dbm=`, so it pairs them,
prints the level per distance bin with its spread, and scores each candidate open
threshold on how well it separates near from far. That is what should set
WOZ_RSSI_GATE_OPEN_DBM / CLOSE_DBM, which ship as placeholders. No analyzer needed.

Parses the same "[ALAB] t=<us> ev=..." trace aliro_lab.py reads (firmware built
with CONFIG_WOZ_ALIRO_LAB, `lab on`), now including the RSSI power-gate events
(ev=rssi/gate.hold/gate.open/gate.close, dbm=...), and reports per walk-up:

  held    connect -> gate.open (auth done, UWB deliberately dark)
  g->bolt gate.open -> bolt (the latency the gate actually costs at the door)
  c->bolt connect -> bolt (whole walk-up)
  uwb-on  m4 -> gate.close/session end (the window the DW3000 is powered)
  duty    uwb-on as % of the connected time
  rssi    smoothed level at gate.open (dBm)

--ppk merges a power-analyzer CSV export (PPK2-style: header line, then
"<t_ms>,<current_uA>" rows) and adds mean mA over idle / held / uwb-on spans.
Alignment: the largest positive current step in the capture is assumed to be
the DW3000 waking at m4 (--shift SECONDS overrides with a manual offset from
capture start to the first m4). --tag labels every row (e.g. the approach
speed: slow/normal/fast) so runs concatenate into one study CSV.

Exit status: 0 = parsed at least one walk-up, 2 = usage/input error.

**discussed in** [`docs/power-profile.md`](../../power-profile.md)

## API

### `parse_log(path)`
`tools/power_profile.py:45`

[(t_us, name, {attr: int})] in line order.

**called by** `main`

### `split_walkups(events)`
`tools/power_profile.py:60`

Group events into walk-ups on session.start boundaries.

**called by** `main`

### `first(run, name, attr=None)`
`tools/power_profile.py:75`

t_us of the first `name` event (and its `attr` value), or (None, None).

**called by** `analyze`

### `analyze(run)`
`tools/power_profile.py:83`

One walk-up -> dict of the report row (times in us where suffixed).

**called by** `main`  ·  **calls** `first`

### `parse_ppk(path)`
`tools/power_profile.py:117`

PPK2-style CSV -> [(t_s, mA)]. Header line skipped; t in ms, I in uA.

**called by** `main`

### `align_ppk(samples, t_m4_us, shift_s)`
`tools/power_profile.py:136`

Offset such that capture time + offset == device time (s). Manual shift
wins; else the largest positive step of the 50 ms-smoothed current is taken
as the DW3000 waking at the first m4.

**called by** `main`

### `span_ma(samples, off_s, a_us, b_us)`
`tools/power_profile.py:160`

Mean mA over device-time span [a_us, b_us]; None if no samples fall in.

**called by** `main`

### `pair_range_rssi(events, window_us)`
`tools/power_profile.py:169`

[(cm, dbm)] — every trusted range paired with the RSSI sample nearest it in
time, within window_us. The UWB range is the ground truth the BLE level has to
be judged against, and both already ride the same trace, so a walk-up capture
is a calibration run whether or not it was meant as one.

**called by** `main`

### `pct(sorted_vals, p)`
`tools/power_profile.py:186`

Nearest-rank percentile of an already-sorted list.

**called by** `calibrate`

### `calibrate(pairs, near_cm, edges)`
`tools/power_profile.py:194`

Print the dBm-to-distance curve and, for each candidate threshold, how well
it separates near from far. Returns the best-margin threshold or None.

**called by** `main`  ·  **calls** `pct`, `print_table`

<details><summary>Undocumented (4)</summary>

- `print_table`
- `fmt_ms`
- `fmt_ma`
- `main`

</details>
