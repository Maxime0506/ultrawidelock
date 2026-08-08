"""Turn `make monitor` captures into the scalar feature set, one class per file.

    python3 ai/tinyml/parse_alab.py <capture.log>              # inspect, write nothing
    python3 ai/tinyml/parse_alab.py --clear a.log --blocked b.log -o door.npz

    <capture.log>   RTT log from the cirdiag image (`make monitor ... | tee <file>`)
    --clear FILE    a run with nothing between phone and reader; repeatable
    --blocked FILE  a run with a body, door or laptop in the path; repeatable
    -o FILE         write an .npz that bakeoff.py and gen_model.py read unchanged

ONE CONDITION PER RUN, one file per run. Labelling by cycle marker inside a single run
was tried and is a trap: it needs the operator to change condition on a 23 s clock, and
a single missed interval silently mislabels everything after it. Capture clear until you
are bored, stop, set up the obstruction, capture again.

BUT A BLOCKED RUN IS NOT ENTIRELY BLOCKED, which is what --blocked-window is for. A
session has to be established before anything is received at all, and establishing it
needs enough signal, so an operator blocking the phone hard enough to matter has to
expose it first. Those exposed receptions land in the blocked file and are physically
clear. Measured 2026-08-07 on the first 300-sample pair: 28% of "blocked" samples fell
inside the clear inter-quartile range while 30% sat below the clear 5th percentile --
one label covering two populations. Cross-validated balanced accuracy was 0.6339 against
a 0.6040 vendor-rule baseline, which reads as a weak feature set and is mostly a third of
one class being mislabelled.

So write down the wall-clock spans during which the obstruction was actually in place and
pass them as --blocked-window. Receptions outside every window are DROPPED, never
relabelled: outside the window the condition is unknown, and guessing there is the
original mistake wearing a hat.

The two classes are the ones modules/woz_ml/include/woz_ml.h already names,
WOZ_ML_LOS_CLEAR (y=0) and WOZ_ML_LOS_OBSTRUCTED (y=1), matching eWINE's LOS/NLOS
convention so a model trained here drops into the same seam.

With no labelled files it prints per-interval statistics and writes nothing, which is how
you check a capture is worth keeping before walking any further.

WHY ONLY FOUR FEATURES. The other ten in extract_features.py need the 64-tap CIR window,
and on the DWM3001CDK the image that reads the accumulator cannot range at all (see
ai/tinyml/RESULTS.md Result 7, which also measures the cost of dropping them: 0.14
accuracy points). These four come from `dwt_readdiagnostics` alone.

THE dB CONSTANTS ARE DW1000's. A_CONST_PRF64 and the 2^17 scaling come from the eWINE
formulas, and the DW3000's ipatovPower is a 17-bit "channel area" that is not the same
quantity as the DW1000's CIR_PWR. That is fine here and only here: the model is retrained
on this data, so a consistent offset moves the fitted thresholds and nothing else. Do NOT
compare these dBm numbers against the eWINE ones.
"""

import argparse
import re
import sys

import numpy as np

from features_io import write_features

# eWINE's constant, kept so the arithmetic matches extract_features.py exactly. See the
# module docstring for why its absolute value does not matter here.
A_CONST_PRF64 = 121.74

FEATURE_NAMES = ["fp_pwr", "rx_pwr", "pwr_diff", "rxpacc"]
# Emitted only with --distance, appended in this order. `fp_resid` is fp_pwr with
# free-space spreading at the measured range removed, which is the single change that
# most improves how the model survives an unseen capture (RESULTS.md Result 11).
DISTANCE_NAMES = ["dist_cm", "fp_resid"]

DIAG_RE = re.compile(r"\[ALAB\][^\n]*\bev=uwb\.diag\b([^\n]*)")
KV_RE = re.compile(r"\b([a-z0-9_]+)=(-?\d+)\b")
CYCLE_RE = re.compile(r"cir\.cycle: n=(\d+) (capture|end|armed|drained)")
# The monitor prefixes each line with wall clock; that is what an operator can write
# down while walking, so it is what the time windows are expressed in.
CLOCK_RE = re.compile(r"(\d{2}):(\d{2}):(\d{2})\.(\d+):")

# The ranging distance is NOT on the [ALAB] lines. It is on the bench TUI's status line,
# printed every ~2 s, which also carries the cumulative successful-reception count. That
# count is the alignment key rather than the wall clock, because probe-rs drains the RTT
# ring on attach and stamps that entire burst with one identical millisecond, so
# timestamps are not monotonic in reception order but the counters are.
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
STATUS_RE = re.compile(r"✓(\d+).*?·\s*(\d+)cm(\s+stale\s+\d+s)?")

# Free-space spreading is 20*log10(d), so adding it back to a dB power normalises every
# reception to what it would have measured at this range. Clamped below because the
# correction diverges at zero and the reader does report 0 cm.
MIN_RANGE_CM = 10.0
REF_RANGE_CM = 100.0
# How old the [ALAB] `dage` may be before the reception is dropped. A DS-TWR round
# completes far more often than this when ranging is healthy, so the threshold is not
# tuning: it is there to drop receptions that carried a distance across a gap in ranging,
# which is a different condition from the one being measured.
STALE_MS = 3000.0


def parse_clock(text):
    """"HH:MM:SS" -> seconds since midnight. Accepts a bare "HH:MM" too."""
    bits = [int(p) for p in text.strip().split(":")]
    if len(bits) == 2:
        bits.append(0)
    if len(bits) != 3:
        raise ValueError(f"not a time: {text!r}")
    return bits[0] * 3600 + bits[1] * 60 + bits[2]


def parse_windows(specs):
    """["03:20:10-03:23:00", ...] -> [(start_s, end_s), ...], empty meaning "no filter"."""
    out = []
    for spec in specs or []:
        for part in spec.split(","):
            part = part.strip()
            if not part:
                continue
            if "-" not in part:
                raise SystemExit(f"window needs START-END, got {part!r}")
            a, b = part.split("-", 1)
            lo, hi = parse_clock(a), parse_clock(b)
            if hi <= lo:
                raise SystemExit(f"window {part!r} ends before it starts")
            out.append((lo, hi))
    return out


def parse_log(path):
    """Return (records, skipped). Each record is the k=v dict plus the cycle it fell in.

    A reception belongs to the cycle whose `capture` marker most recently preceded it;
    receptions after an `end` and before the next `capture` are in the gap and carry
    cycle None, because that is when the operator is repositioning and the label does
    not hold.
    """
    records = []
    skipped = 0
    cycle = None

    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = CYCLE_RE.search(line)
            if m:
                cycle = int(m.group(1)) if m.group(2) in ("capture", "armed") else None
                continue
            m = DIAG_RE.search(line)
            if not m:
                continue
            clock = CLOCK_RE.search(line)
            kv = {k: int(v) for k, v in KV_RE.findall(m.group(1))}
            kv["clock"] = (int(clock.group(1)) * 3600 + int(clock.group(2)) * 60
                           + int(clock.group(3))) if clock else -1
            if not {"ipf1", "ipf2", "ipf3", "ipac", "ippw"} <= kv.keys():
                skipped += 1
                continue
            kv["cycle"] = cycle
            records.append(kv)
    return records, skipped


def status_points(path):
    """(rx_count, distance_cm) for every FRESH status line, in reception order.

    Lines reading `stale Ns` carry a distance from N seconds ago and are dropped: a stale
    reading is the reader's own statement that it does not know the current range.
    """
    pts = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = STATUS_RE.search(ANSI_RE.sub("", line))
            if m and not m.group(3):
                pts.append((int(m.group(1)), float(m.group(2))))
    return np.array(pts, dtype=float) if pts else np.empty((0, 2))


def distances(path, records, every):
    """Per-reception range in cm, NaN where none can be attributed to the reception.

    TWO SOURCES, and the good one is preferred silently. Firmware built after the `d=`
    field was added puts the DS-TWR distance on the [ALAB] line itself, along with `dage`,
    its age in milliseconds. That needs no alignment at all: the range arrives attached to
    the reception it belongs to, and a plain `make monitor` capture carries it.

    Older captures have neither key, and for those the range has to be recovered by
    scraping the bench TUI's RENDERED status line and interpolating on the reception
    counter, which is why every capture before this had to be made with `make openaliro`.
    That path is kept because the tracked 2026-08-07 set was made that way and has to stay
    reproducible; it is not the path to use for new work.

    `every` is CONFIG_WOZ_UWB_CIRDIAG_SUMMARY_EVERY and only matters to the old path: the
    firmware emits one [ALAB] line per `every` receptions, so record `n` sits at rx count
    ~n*every. Both sequences are monotonic in that counter, so a piecewise-linear
    interpolation between status lines is the natural association, and it extrapolates to
    neither end.
    """
    if any("d" in r for r in records):
        d = np.array([float(r.get("d", np.nan)) for r in records])
        age = np.array([float(r.get("dage", np.nan)) for r in records])
        # A range older than the round that would replace it is not this reception's
        # range. STALE_MS is deliberately generous: the point is to drop the reception
        # that kept a distance across a gap in ranging, not to thin the set.
        stale = np.count_nonzero(age > STALE_MS)
        missing = np.count_nonzero(np.isnan(d))
        print(f"{path}: range from the [ALAB] line ({len(d) - missing - stale} usable, "
              f"{missing} with no round yet, {stale} older than {STALE_MS} ms)")
        return np.where(np.isnan(age) | (age > STALE_MS), np.nan, d)

    pts = status_points(path)
    if len(pts) < 2:
        raise SystemExit(
            f"{path}: found {len(pts)} usable status line(s), need at least 2 for "
            f"--distance. Capture with the bench TUI (`make openaliro`), which prints "
            f"them; plain `make monitor` output carries no range.")
    ns = np.array([r["n"] for r in records], dtype=float)
    return np.interp(ns * every, pts[:, 0], pts[:, 1], left=np.nan, right=np.nan)


def path_loss_residual(fp_pwr, dist_cm):
    """fp_pwr with free-space spreading at `dist_cm` removed, referenced to 1 m."""
    return fp_pwr + 20.0 * np.log10(np.maximum(dist_cm, MIN_RANGE_CM) / REF_RANGE_CM)


def features(records):
    """The four scalar features, in FEATURE_NAMES order."""
    f1 = np.array([r["ipf1"] for r in records], dtype=np.float64)
    f2 = np.array([r["ipf2"] for r in records], dtype=np.float64)
    f3 = np.array([r["ipf3"] for r in records], dtype=np.float64)
    acc = np.array([r["ipac"] for r in records], dtype=np.float64)
    pwr = np.array([r["ippw"] for r in records], dtype=np.float64)

    # A zero accumulator count or a zero channel area is a failed CIA read, not a
    # measurement of a very weak channel. Left in, log10 turns them into -120 dB
    # outliers that dominate the mean and inflate the spread by 20 dB; NaN drops the
    # reception instead. This is not hypothetical -- the CPER-set receptions in a real
    # capture report ippw=0.
    acc = np.where(acc > 0, acc, np.nan)
    pwr = np.where(pwr > 0, pwr, np.nan)
    num = f1**2 + f2**2 + f3**2
    num = np.where(num > 0, num, np.nan)
    fp_pwr = 10.0 * np.log10(num / acc**2) - A_CONST_PRF64
    rx_pwr = 10.0 * np.log10(pwr * (2.0**17) / acc**2) - A_CONST_PRF64
    return np.column_stack([fp_pwr, rx_pwr, rx_pwr - fp_pwr, acc])


def load(path, label, want_len=None, windows=None, every=None):
    """One file's usable receptions as (X, y, frame lengths, report lines).

    `windows` is a list of (start, end) wall-clock seconds. When given, receptions
    OUTSIDE every window are dropped rather than relabelled: outside the window the
    condition is unknown, and a guess there is exactly the mistake this argument exists
    to prevent (see the module docstring on the exposed phase).
    """
    records, skipped = parse_log(path)
    if not records:
        sys.exit(f"{path}: no [ALAB] ev=uwb.diag lines found")
    X = features(records)
    if every is not None:
        d = distances(path, records, every)
        X = np.column_stack([X, d, path_loss_residual(X[:, 0], d)])
    cycles = np.array([-1 if r["cycle"] is None else r["cycle"] for r in records])
    lens = np.array([r.get("len", -1) for r in records])
    clocks = np.array([r.get("clock", -1) for r in records])
    finite = np.isfinite(X).all(axis=1)
    if want_len is not None:
        finite &= lens == want_len
    if windows:
        inside = np.zeros(len(records), dtype=bool)
        for lo, hi in windows:
            inside |= (clocks >= lo) & (clocks <= hi)
        finite &= inside

    lines = [f"{path}: {len(records)} reception(s), {int(finite.sum())} usable"
             f"{f', {skipped} malformed' if skipped else ''}"
             f"{'' if label is None else f' -> {LABEL_NAMES[label]}'}"]
    lines.append("  cycle    n   fp_pwr dB      rx_pwr dB    pwr_diff dB")
    for c in sorted(set(cycles.tolist())):
        sel = (cycles == c) & finite
        if not sel.any():
            continue
        s = X[sel]
        name = "(gap)" if c < 0 else str(c)
        lines.append(f"  {name:>5} {sel.sum():>4}   {s[:, 0].mean():7.2f}±{s[:, 0].std():4.2f}"
                     f"  {s[:, 1].mean():7.2f}±{s[:, 1].std():4.2f}"
                     f"  {s[:, 2].mean():7.2f}±{s[:, 2].std():4.2f}")

    y = np.full(int(finite.sum()), -1 if label is None else label, dtype=np.int64)
    return X[finite], y, lens[finite], lines


LABEL_NAMES = {0: "clear", 1: "blocked"}

# Largest tolerable gap between the two classes' share of any one frame length. Above it
# the classes differ in what they are made of, not only in the channel they travelled.
FRAME_MIX_TOL = 0.15


def frame_mix_report(lens, y):
    """Per-class frame-length mix, and a warning when the classes do not match.

    THIS EXISTS BECAUSE THE CONFOUND ALMOST SHIPPED. On 2026-08-07 the first labelled
    pair was 94% len=0 in the clear class and 24% in the blocked one, because the clear
    capture predated the fix that let rounds complete: a broken round retries POLLs
    (len=0 RFRAMEs) while a working one carries Pre-POLL and Final_Data too. Pooled, the
    strongest-looking feature was rxpacc at d=-1.01 and 0.800 balanced accuracy. Split by
    frame length it collapses to d~0.2: it was measuring frame type, not obstruction. A
    model trained on that mixture would have scored well and learned the wrong thing.

    fp_pwr and pwr_diff survived the split at d=-1.2..-3.3 and +0.95..+1.44, consistently
    signed in every stratum, which is what a real physical effect looks like.
    """
    out, mixes = [], {}
    for label in (0, 1):
        sel = y == label
        if not sel.any():
            continue
        vals, counts = np.unique(lens[sel], return_counts=True)
        share = counts / counts.sum()
        mixes[label] = dict(zip(vals.tolist(), share.tolist()))
        parts = " ".join(f"len={v}:{c}({s:.0%})" for v, c, s in zip(vals, counts, share))
        out.append(f"  {LABEL_NAMES[label]:>7}: {parts}")

    warn = False
    if len(mixes) == 2:
        for v in set(mixes[0]) | set(mixes[1]):
            if abs(mixes[0].get(v, 0.0) - mixes[1].get(v, 0.0)) > FRAME_MIX_TOL:
                warn = True
    if warn:
        out.append("  WARNING: the classes differ in FRAME MIX, not only in channel.")
        out.append("  Any model fitted here can score well by learning frame type instead")
        out.append("  of obstruction. Re-run with --frame-len to compare like for like,")
        out.append("  or recapture so both classes see the same traffic.")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", nargs="?", help="inspect one capture without labelling it")
    ap.add_argument("--clear", action="append", metavar="FILE", default=[])
    ap.add_argument("--blocked", action="append", metavar="FILE", default=[])
    ap.add_argument("-o", "--out", metavar="FILE")
    ap.add_argument("--frame-len", type=int, metavar="N",
                    help="keep only receptions of this frame length, so the classes are"
                         " compared like for like (0 is the STS-only RFRAME)")
    ap.add_argument("--clear-window", action="append", metavar="HH:MM:SS-HH:MM:SS",
                    default=[], help="within --clear files, keep only these wall-clock"
                                     " spans; everything else is DROPPED, not relabelled")
    ap.add_argument("--blocked-window", action="append", metavar="HH:MM:SS-HH:MM:SS",
                    default=[], help="same, for --blocked files")
    ap.add_argument("--distance", nargs="?", type=int, const=8, metavar="EVERY",
                    help="also emit dist_cm and fp_resid, read from the bench TUI's"
                         " status lines and aligned on its reception counter. EVERY is"
                         " CONFIG_WOZ_UWB_CIRDIAG_SUMMARY_EVERY (default 8). Needs a"
                         " capture made with `make openaliro`; plain `make monitor`"
                         " output carries no range")
    args = ap.parse_args()

    cw, bw = parse_windows(args.clear_window), parse_windows(args.blocked_window)
    jobs = [(p, 0, cw) for p in args.clear] + [(p, 1, bw) for p in args.blocked]
    if args.log:
        jobs.append((args.log, None, None))
    if not jobs:
        ap.error("give a capture to inspect, or --clear/--blocked files to label")

    parts, report = [], []
    for path, label, win in jobs:
        X, y, lens, lines = load(path, label, args.frame_len, win, args.distance)
        report.extend(lines)
        parts.append((X, y, lens))
    print("\n".join(report))

    X = np.vstack([p[0] for p in parts])
    y = np.concatenate([p[1] for p in parts])
    lens = np.concatenate([p[2] for p in parts])
    keep = y >= 0
    if not keep.any():
        print("\nNo labelled files given, so nothing was written."
              " Re-run with --clear and --blocked.")
        return

    n0, n1 = int((y == 0).sum()), int((y == 1).sum())
    print(f"\nlabelled {int(keep.sum())}: {n0} clear, {n1} blocked"
          f"{'' if args.frame_len is None else f' (len={args.frame_len} only)'}")
    print("frame mix:")
    print("\n".join(frame_mix_report(lens[keep], y[keep])))

    if min(n0, n1) == 0:
        sys.exit("BOTH classes are needed to fit anything; one of them is empty.")
    if min(n0, n1) < 50:
        print("WARNING: fewer than 50 in a class. Thin for a fit; collect more.")

    if args.out:
        names = FEATURE_NAMES + (DISTANCE_NAMES if args.distance else [])
        write_features(args.out, X[keep], y[keep], lens[keep], names)
        print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
