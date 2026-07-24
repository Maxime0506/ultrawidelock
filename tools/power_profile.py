#!/usr/bin/env python3
"""Power profile: turn a gated-walk-up serial log (+ optional power capture)
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
"""

import re
import sys

ALAB_RE = re.compile(r"\[ALAB\] t=(\d+) ev=(\S+)((?: \S+=-?\d+)*)")
ATTR_RE = re.compile(r"(\S+)=(-?\d+)")


def parse_log(path):
    """[(t_us, name, {attr: int})] in line order."""
    events = []
    try:
        with open(path, "r", errors="replace") as f:
            for line in f:
                m = ALAB_RE.search(line)
                if m:
                    attrs = dict((k, int(v)) for k, v in ATTR_RE.findall(m.group(3)))
                    events.append((int(m.group(1)), m.group(2), attrs))
    except OSError as e:
        sys.exit("power_profile: %s" % e)
    return events


def split_walkups(events):
    """Group events into walk-ups on session.start boundaries."""
    runs, cur = [], None
    for ev in events:
        if ev[1] == "session.start":
            if cur:
                runs.append(cur)
            cur = []
        if cur is not None:
            cur.append(ev)
    if cur:
        runs.append(cur)
    return [r for r in runs if any(e[1] == "ph.connect" for e in r)]


def first(run, name, attr=None):
    """t_us of the first `name` event (and its `attr` value), or (None, None)."""
    for t, n, attrs in run:
        if n == name:
            return t, attrs.get(attr) if attr else None
    return None, None


def analyze(run):
    """One walk-up -> dict of the report row (times in us where suffixed)."""
    t_connect, _ = first(run, "ph.connect")
    t_hold, _ = first(run, "gate.hold")
    t_open, open_dbm = first(run, "gate.open", "dbm")
    if t_open is None:
        # The hold hit CONFIG_WOZ_RSSI_GATE_MAX_HOLD_MS and the reader completed the
        # AP anyway. That is still the moment the radio was allowed up, so it is the
        # end of the held span; open_dbm then reads as the level we gave up at.
        t_open, open_dbm = first(run, "gate.holdcap", "dbm")
    t_m4, _ = first(run, "ph.m4")
    t_bolt, _ = first(run, "ph.bolt")
    t_close, _ = first(run, "gate.close")
    t_end = run[-1][0]

    # Gate already open at auth completion: no hold, no gate.open event.
    held_us = (t_open - t_connect) if (t_hold is not None and t_open is not None and
                                       t_connect is not None) else 0
    uwb_off = t_close if t_close is not None else t_end
    return {
        "held_us": held_us,
        "gate_bolt_us": (t_bolt - t_open) if (t_open is not None and t_bolt is not None) else None,
        "conn_bolt_us": (t_bolt - t_connect) if (t_connect is not None and
                                                 t_bolt is not None) else None,
        "uwb_on_us": (uwb_off - t_m4) if t_m4 is not None else 0,
        "conn_us": (t_end - t_connect) if t_connect is not None else None,
        "open_dbm": open_dbm,
        "t_connect": t_connect,
        "t_open": t_open,
        "t_m4": t_m4,
        "uwb_off": uwb_off,
    }


def parse_ppk(path):
    """PPK2-style CSV -> [(t_s, mA)]. Header line skipped; t in ms, I in uA."""
    samples = []
    try:
        with open(path, "r", errors="replace") as f:
            for i, line in enumerate(f):
                parts = line.strip().split(",")
                if len(parts) < 2:
                    continue
                try:
                    samples.append((float(parts[0]) / 1e3, float(parts[1]) / 1e3))
                except ValueError:
                    if i > 0:  # only the header may be non-numeric
                        pass
    except OSError as e:
        sys.exit("power_profile: %s" % e)
    return samples


def align_ppk(samples, t_m4_us, shift_s):
    """Offset such that capture time + offset == device time (s). Manual shift
    wins; else the largest positive step of the 50 ms-smoothed current is taken
    as the DW3000 waking at the first m4."""
    if shift_s is not None:
        return t_m4_us / 1e6 - shift_s
    if not samples or t_m4_us is None:
        return None
    # 50 ms boxcar, then the largest rise between consecutive windows.
    win, means, t0 = 0.05, [], samples[0][0]
    acc, n, wt = 0.0, 0, t0
    for t, ma in samples:
        if t - wt >= win and n:
            means.append((wt, acc / n))
            acc, n, wt = 0.0, 0, t
        acc += ma
        n += 1
    best, best_t = 0.0, None
    for a, b in zip(means, means[1:]):
        if b[1] - a[1] > best:
            best, best_t = b[1] - a[1], b[0]
    return None if best_t is None else t_m4_us / 1e6 - best_t


def span_ma(samples, off_s, a_us, b_us):
    """Mean mA over device-time span [a_us, b_us]; None if no samples fall in."""
    if off_s is None or a_us is None or b_us is None or b_us <= a_us:
        return None
    lo, hi = a_us / 1e6 - off_s, b_us / 1e6 - off_s
    vals = [ma for t, ma in samples if lo <= t < hi]
    return (sum(vals) / len(vals)) if vals else None


def pair_range_rssi(events, window_us):
    """[(cm, dbm)] — every trusted range paired with the RSSI sample nearest it in
    time, within window_us. The UWB range is the ground truth the BLE level has to
    be judged against, and both already ride the same trace, so a walk-up capture
    is a calibration run whether or not it was meant as one."""
    ranges = [(t, a["cm"]) for t, n, a in events if n == "range" and "cm" in a]
    rssis = [(t, a["dbm"]) for t, n, a in events if n == "rssi" and "dbm" in a]
    pairs = []
    j = 0
    for t, cm in ranges:
        while j + 1 < len(rssis) and abs(rssis[j + 1][0] - t) <= abs(rssis[j][0] - t):
            j += 1
        if rssis and abs(rssis[j][0] - t) <= window_us:
            pairs.append((cm, rssis[j][1]))
    return pairs


def pct(sorted_vals, p):
    """Nearest-rank percentile of an already-sorted list."""
    if not sorted_vals:
        return None
    k = int(round((p / 100.0) * (len(sorted_vals) - 1)))
    return sorted_vals[k]


def calibrate(pairs, near_cm, edges):
    """Print the dBm-to-distance curve and, for each candidate threshold, how well
    it separates near from far. Returns the best-margin threshold or None."""
    print("calibration: %d paired samples (range + nearest RSSI)\n" % len(pairs))

    hdr = ["distance", "n", "median", "p10", "p90", "min", "max"]
    table = []
    for lo, hi in zip(edges, edges[1:] + [None]):
        vals = sorted(d for cm, d in pairs if cm >= lo and (hi is None or cm < hi))
        if not vals:
            continue
        label = ("%d-%d cm" % (lo, hi)) if hi is not None else "%d+ cm" % lo
        table.append([label, str(len(vals)), str(pct(vals, 50)), str(pct(vals, 10)),
                      str(pct(vals, 90)), str(vals[0]), str(vals[-1])])
    print_table(hdr, table)

    near = [d for cm, d in pairs if cm < near_cm]
    far = [d for cm, d in pairs if cm >= near_cm]
    if not near or not far:
        print("\nno separation table: need samples on both sides of %d cm" % near_cm)
        return None

    print("\nseparation at %d cm  (%d near, %d far)" % (near_cm, len(near), len(far)))
    hdr2 = ["open dBm", "opens near", "opens far (false)", "margin"]
    rows, best = [], None
    for thr in range(-40, -91, -5):
        n_rate = 100.0 * sum(1 for d in near if d >= thr) / len(near)
        f_rate = 100.0 * sum(1 for d in far if d >= thr) / len(far)
        rows.append([str(thr), "%.0f%%" % n_rate, "%.0f%%" % f_rate,
                     "%+.0f" % (n_rate - f_rate)])
        if best is None or (n_rate - f_rate) > best[1]:
            best = (thr, n_rate - f_rate, n_rate, f_rate)
    print_table(hdr2, rows)

    print("\nbest margin: %d dBm — opens for %.0f%% of near samples, %.0f%% of far"
          % (best[0], best[2], best[3]))
    if best[1] < 50.0:
        print("WEAK SEPARATION: BLE RSSI does not cleanly tell these distances apart\n"
              "here. A threshold set from this data will either hold too long or open\n"
              "too early; treat the gate as a coarse pre-filter, not a range estimate.")
    return best[0]


def print_table(hdr, rows):
    widths = [max(len(h), max((len(r[c]) for r in rows), default=0))
              for c, h in enumerate(hdr)]
    print("| " + " | ".join(h.ljust(w) for h, w in zip(hdr, widths)) + " |")
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for r in rows:
        print("| " + " | ".join(v.ljust(w) for v, w in zip(r, widths)) + " |")


def fmt_ms(us):
    return "-" if us is None else "%.0f" % (us / 1e3)


def fmt_ma(ma):
    return "-" if ma is None else "%.1f" % ma


def main(argv):
    args, log, ppk, tag, shift, csv_out = argv[1:], None, None, "", None, None
    do_cal, near_cm, pair_ms = False, 100, 300
    it = iter(args)
    for a in it:
        if a == "--ppk":
            ppk = next(it, None)
        elif a == "--tag":
            tag = next(it, "")
        elif a == "--shift":
            shift = float(next(it, "0"))
        elif a == "--csv":
            csv_out = next(it, None)
        elif a == "--calibrate":
            do_cal = True
        elif a == "--near-cm":
            near_cm = int(next(it, "100"))
        elif a == "--pair-ms":
            pair_ms = int(next(it, "300"))
        elif a.startswith("-"):
            sys.exit(__doc__.strip().split("\n")[0] + "\n(unknown option %s)" % a)
        else:
            log = a
    if log is None:
        sys.exit("usage: power_profile.py <capture.log> [--ppk trace.csv] "
                 "[--tag LABEL] [--shift SECONDS] [--csv out.csv]\n"
                 "       power_profile.py <capture.log> --calibrate "
                 "[--near-cm CM] [--pair-ms MS]")

    if do_cal:
        pairs = pair_range_rssi(parse_log(log), pair_ms * 1000)
        if not pairs:
            sys.exit("power_profile: no range/rssi pairs (needs a walk-up that ranged, "
                     "with `lab on`)")
        calibrate(pairs, near_cm, [0, 50, 100, 150, 200, 250, 300])
        return 0

    runs = split_walkups(parse_log(log))
    if not runs:
        sys.exit("power_profile: no walk-ups (is the firmware's `lab on` trace in this log?)")

    samples = parse_ppk(ppk) if ppk else []
    rows = []
    for run in runs:
        r = analyze(run)
        off = align_ppk(samples, r["t_m4"], shift) if samples else None
        # idle = the second before connect; held = connect -> gate.open (or m4)
        r["idle_ma"] = span_ma(samples, off, (r["t_connect"] or 0) - 1_000_000,
                               r["t_connect"])
        r["held_ma"] = span_ma(samples, off, r["t_connect"],
                               r["t_open"] if r["t_open"] is not None else r["t_m4"])
        r["uwb_ma"] = span_ma(samples, off, r["t_m4"],
                              r["uwb_off"] if r["uwb_on_us"] else None)
        rows.append(r)

    hdr = ["run", "tag", "held ms", "g->bolt ms", "c->bolt ms", "uwb-on ms", "duty %",
           "open dBm", "idle mA", "held mA", "uwb mA"]
    table = []
    for i, r in enumerate(rows, 1):
        duty = ("%.0f" % (100.0 * r["uwb_on_us"] / r["conn_us"])) if r["conn_us"] else "-"
        table.append([str(i), tag, fmt_ms(r["held_us"]), fmt_ms(r["gate_bolt_us"]),
                      fmt_ms(r["conn_bolt_us"]), fmt_ms(r["uwb_on_us"]), duty,
                      str(r["open_dbm"]) if r["open_dbm"] is not None else "-",
                      fmt_ma(r["idle_ma"]), fmt_ma(r["held_ma"]), fmt_ma(r["uwb_ma"])])

    print_table(hdr, table)

    if csv_out:
        with open(csv_out, "a") as f:
            if f.tell() == 0:
                f.write(",".join(h.replace(" ", "_") for h in hdr) + "\n")
            for t in table:
                f.write(",".join(t) + "\n")
        print("\nappended %d row(s) to %s" % (len(table), csv_out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
