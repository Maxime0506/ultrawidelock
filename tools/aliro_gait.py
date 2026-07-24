#!/usr/bin/env python3
"""Aliro Gait: carry-motion features from Aliro Lab walk-up captures.

Usage: python3 tools/aliro_gait.py [-o report.html] [label=]capture.log ...

E1 probe of the passive carry verification experiment (see
internal/passive-verify-scoping.md): for every walk-up transaction in the
given "[ALAB]" captures, detrend the per-block trusted-range series, FFT the
residual, and report the carry-motion features (cadence, stride regularity,
approach speed, deceleration, closest approach, residual RMS) plus a
per-window carried/stationary verdict. With two or more labels (one per
carrier, e.g. alice=alice.log bob=bob.log) it also runs leave-one-out
nearest-centroid classification to measure whether the features separate the
carriers — the pre-registered Tier-2 GO bar is >= 80%.

The block duration (and the phone's implied RAN multiplier) is derived from
the range timestamps themselves, so no extra firmware logging is needed.
Exit status: 0 = report produced, 2 = usage/input error.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aliro_lab  # noqa: E402  (same tools/ directory: parser + txn model)

# Cadence search band (Hz): normal walking is ~1.6-2.2 steps/s; the band is
# wider to catch slow/hurried gaits, and its top is clipped below Nyquist for
# slow captures. Baseline for peak prominence starts lower so trend leakage
# counts against the peak instead of flattering it.
GAIT_BAND_LO_HZ = 1.2
GAIT_BAND_HI_HZ = 2.6
BASELINE_LO_HZ = 0.6
MIN_PEAK_HALF_WIDTH_HZ = 0.15
MIN_BAND_WIDTH_HZ = 0.2

NFFT = 256          # zero-padded DFT length (fine peak grid, ~0.02 Hz bins)
MIN_SAMPLES = 12    # below this a window has no usable spectrum

# Per-window carry-motion verdict rides on residual RMS alone: a static
# channel jitters ~1 cm while a carried phone's residual rides at several cm.
# Prominence is deliberately NOT part of the verdict — the min-spectrum ratio
# is scale-free, so short pure-noise windows can look "peaky" too; it is a
# cadence quality weight for Tier 2, not a stationary discriminator.
# Thresholds are provisional until E1 data lands.
MOTION_MIN_RMS_CM = 2.0
APPROACH_MIN_SPEED_CM_S = 15.0

# Incremental estimator (the shape firmware can afford per block): Goertzel
# bank step and trailing-window length.
INC_STEP_HZ = 0.05
INC_WINDOW = 24

# Feature vector used for classification, in report order.
FEATURES = [
    ("cadence_hz", "cadence", "Hz"),
    ("regularity", "regularity", ""),
    ("rms_cm", "residual RMS", "cm"),
    ("speed_cm_s", "closing speed", "cm/s"),
    ("accel_cm_s2", "accel", "cm/s^2"),
    ("min_cm", "closest", "cm"),
]


class WalkUp:
    """One analyzed walk-up: identity, series shape, features (or skip reason)."""

    def __init__(self, label, txn_index):
        self.label = label
        self.txn_index = txn_index
        self.n = 0
        self.dur_s = 0.0
        self.block_ms = None
        self.ran = None
        self.features = None
        self.skip = None


# ---- small numeric helpers (stdlib only, series are <100 samples) ----

def _median(xs):
    s = sorted(xs)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


def _solve3(a, b):
    """Gaussian elimination for the 3x3 normal equations of the quadratic fit."""
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(3):
        piv = max(range(col, 3), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-12:
            return None
        m[col], m[piv] = m[piv], m[col]
        for r in range(3):
            if r == col:
                continue
            f = m[r][col] / m[col][col]
            for c in range(col, 4):
                m[r][c] -= f * m[col][c]
    return [m[i][3] / m[i][i] for i in range(3)]


def _polyfit2(ts, xs):
    """Least-squares x(t) = a*t^2 + b*t + c; returns (a, b, c)."""
    s = [0.0] * 5
    for t in ts:
        p = 1.0
        for k in range(5):
            s[k] += p
            p *= t
    r = [0.0] * 3
    for t, x in zip(ts, xs):
        r[0] += x
        r[1] += x * t
        r[2] += x * t * t
    sol = _solve3([[s[2], s[1], s[0]],
                   [s[3], s[2], s[1]],
                   [s[4], s[3], s[2]]], [r[0], r[1], r[2]])
    if sol is None:
        return 0.0, 0.0, _median(xs)
    return sol[0], sol[1], sol[2]


def _resample(ts, xs, dt):
    """Linear interpolation onto a uniform dt grid (missed blocks leave gaps
    in the capture; the spectrum needs even spacing)."""
    out_t, out_x = [], []
    t = ts[0]
    j = 0
    while t <= ts[-1] + 1e-9:
        while j + 1 < len(ts) and ts[j + 1] < t:
            j += 1
        if j + 1 >= len(ts):
            out_t.append(t)
            out_x.append(xs[-1])
        else:
            span = ts[j + 1] - ts[j]
            f = 0.0 if span <= 0 else (t - ts[j]) / span
            out_t.append(t)
            out_x.append(xs[j] + f * (xs[j + 1] - xs[j]))
        t += dt
    return out_t, out_x


def _hann(n):
    if n < 2:
        return [1.0] * n
    return [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1)) for i in range(n)]


def _dft_power(xs, nfft):
    """Power spectrum, bins 0..nfft/2 (input zero-padded to nfft).
    O(N*K) is fine at these sizes; keeps the tool stdlib-only."""
    n = len(xs)
    powers = []
    for k in range(nfft // 2 + 1):
        re = im = 0.0
        w = -2.0 * math.pi * k / nfft
        for i in range(n):
            re += xs[i] * math.cos(w * i)
            im += xs[i] * math.sin(w * i)
        powers.append(re * re + im * im)
    return powers


def _norm_spectrum(xs, nfft):
    """Hann-windowed magnitude spectrum normalized by coherent gain, so a
    unit-amplitude sine at a bin center reads ~1.0 regardless of length."""
    win = _hann(len(xs))
    gain = max(sum(win) / 2.0, 1e-9)
    p = _dft_power([v * w for v, w in zip(xs, win)], nfft)
    return [math.sqrt(v) / gain for v in p]


def _diff_spectrum(xs, fs, nfft):
    """Spectrum of the first-differenced series, gain-compensated back to
    amplitude units. Differencing crushes what the polynomial detrend leaves
    below the gait band (slow multipath/body-shadow wander) — the dominant
    source of false cadence peaks — and dividing by the difference filter's
    gain |H| = 2 sin(pi f / fs) undoes its tilt so the peak location is
    unbiased. DC (gain 0) is pinned to zero, which the band never reaches."""
    d = [xs[i] - xs[i - 1] for i in range(1, len(xs))]
    mags = _norm_spectrum(d, nfft)
    out = [0.0]
    for k in range(1, len(mags)):
        g = 2.0 * math.sin(math.pi * k / nfft)
        out.append(mags[k] / max(g, 1e-3))
    return out


def _gait_spectra(resid, fs, nfft):
    """(coincidence, full) magnitude spectra of the residual. The coincidence
    spectrum is the min() of the two half-window spectra: at ~26-sample
    windows and SNR ~1 a single periodogram's noise lobes can out-peak the
    true cadence line, but a noise lobe rarely lands on the same frequency in
    both halves, while the gait line is in both by construction. The caller
    picks the coarse peak on the min() and refines its location on the
    full-window spectrum, which has the sharper mainlobe."""
    n = len(resid)
    m = max(MIN_SAMPLES // 2, n // 2)
    coin = [min(a, b) for a, b in
            zip(_diff_spectrum(resid[:m], fs, nfft),
                _diff_spectrum(resid[n - m:], fs, nfft))]
    return coin, _diff_spectrum(resid, fs, nfft)


def _goertzel_power(xs, f_hz, fs_hz):
    coeff = 2.0 * math.cos(2.0 * math.pi * f_hz / fs_hz)
    s1 = s2 = 0.0
    for x in xs:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def incremental_cadence_hz(xs, fs_hz, band_lo, band_hi):
    """Firmware-shaped estimator: first-difference high-pass (kills the
    approach trend), Hann weight over the trailing window, Goertzel bank
    across the gait band. This is what a ring buffer plus a few dozen
    multiplies per block can afford on target; test_aliro_gait.py holds it
    to the FFT answer so the future firmware learner mirrors this exactly."""
    if len(xs) < MIN_SAMPLES:
        return None
    d = [xs[i] - xs[i - 1] for i in range(1, len(xs))]
    d = d[-INC_WINDOW:]
    w = _hann(len(d))
    d = [v * w[i] for i, v in enumerate(d)]
    best_f, best_p = None, -1.0
    f = band_lo
    while f <= band_hi + 1e-9:
        p = _goertzel_power(d, f, fs_hz)
        if p > best_p:
            best_f, best_p = f, p
        f += INC_STEP_HZ
    return best_f


# ---- per-walk-up analysis ----

def analyze_walkup(txn, label):
    """Features from one transaction's trusted-range series. The series is cut
    at the bolt stamp when present: post-unlock loitering at the door is not
    part of the approach."""
    w = WalkUp(label, txn.index)
    ranges = txn.ranges
    bolt = txn.phases.get("bolt")
    if bolt is not None:
        ranges = [(t, cm) for t, cm in ranges if t <= bolt]
    w.n = len(ranges)
    if w.n < MIN_SAMPLES:
        w.skip = "too few range samples (%d < %d)" % (w.n, MIN_SAMPLES)
        return w

    ts = [(t - ranges[0][0]) / 1e6 for t, _ in ranges]
    xs = [float(cm) for _, cm in ranges]
    w.dur_s = ts[-1]
    dts = [ts[i] - ts[i - 1] for i in range(1, len(ts)) if ts[i] > ts[i - 1]]
    if not dts:
        w.skip = "degenerate timestamps"
        return w
    dt = _median(dts)
    fs = 1.0 / dt
    w.block_ms = int(round(dt * 1000.0))
    w.ran = max(1, int(round(w.block_ms / 96.0)))

    band_hi = min(GAIT_BAND_HI_HZ, 0.95 * fs / 2.0)
    if band_hi - GAIT_BAND_LO_HZ < MIN_BAND_WIDTH_HZ:
        w.skip = "gait band above Nyquist (fs %.2f Hz)" % fs
        return w

    rts, rxs = _resample(ts, xs, dt)
    a, b, _c = _polyfit2(rts, rxs)
    resid = [x - (a * t * t + b * t + _c) for t, x in zip(rts, rxs)]
    rms = math.sqrt(sum(v * v for v in resid) / len(resid))
    # Mean closing speed over the window (fit endpoints), deceleration from
    # the quadratic term; positive speed = approaching the door.
    span = rts[-1] if rts[-1] > 0 else 1.0
    speed = -(a * span + b)
    accel = 2.0 * a
    smoothed = [_median(xs[max(0, i - 1):i + 2]) for i in range(len(xs))]
    min_cm = min(smoothed)

    mags, full = _gait_spectra(resid, fs, NFFT)
    freq = [k * fs / NFFT for k in range(len(mags))]
    # Half-window mainlobes are wide; the peak neighborhood scales with the
    # half-power width of the coincidence segments (~fs / (n/2)).
    half = max(MIN_PEAK_HALF_WIDTH_HZ, 2.0 * fs / len(resid))
    band = [k for k, f in enumerate(freq) if GAIT_BAND_LO_HZ <= f <= band_hi]
    coarse_k = max(band, key=lambda k: mags[k])
    # Coarse pick on the coincidence spectrum, fine location on the sharper
    # full-window spectrum near it.
    near_k = [k for k in band if abs(freq[k] - freq[coarse_k]) <= half]
    peak_k = max(near_k, key=lambda k: full[k])
    cadence = freq[peak_k]
    base = [mags[k] for k, f in enumerate(freq)
            if BASELINE_LO_HZ <= f <= band_hi and abs(f - cadence) > half]
    baseline = _median(base) if base else 0.0
    prominence = mags[coarse_k] / max(baseline, 1e-9)
    band_e = sum(mags[k] ** 2 for k in band)
    near_e = sum(mags[k] ** 2 for k in band
                 if abs(freq[k] - cadence) <= half)
    regularity = near_e / band_e if band_e > 0 else 0.0

    inc = incremental_cadence_hz(rxs, fs, GAIT_BAND_LO_HZ, band_hi)
    w.features = {
        "cadence_hz": cadence,
        "inc_cadence_hz": inc,
        "regularity": regularity,
        "rms_cm": rms,
        "speed_cm_s": speed,
        "accel_cm_s2": accel,
        "min_cm": min_cm,
        "prominence": prominence,
        "motion": rms >= MOTION_MIN_RMS_CM,
        "approach": speed >= APPROACH_MIN_SPEED_CM_S,
    }
    return w


def load_walkups(labeled_paths):
    walkups = []
    for label, path in labeled_paths:
        with open(path, "r", errors="replace") as f:
            text = f.read()
        for txn in aliro_lab.split_transactions(aliro_lab.parse_events(text)):
            walkups.append(analyze_walkup(txn, label))
    return walkups


# ---- leave-one-out nearest-centroid classification ----

def classify(walkups):
    """Leave-one-out nearest-centroid over z-scored features. Returns None
    unless there are >= 2 labels with >= 2 analyzed walk-ups each."""
    rows = [(w.label, [w.features[k] for k, _, _ in FEATURES])
            for w in walkups if w.features]
    counts = {}
    for lab, _ in rows:
        counts[lab] = counts.get(lab, 0) + 1
    # A label with a single analyzed walk-up (e.g. the stationary hall-table
    # control) cannot be a leave-one-out training class; drop it from the
    # classification rather than letting it block the carriers'.
    rows = [(lab, v) for lab, v in rows if counts[lab] >= 2]
    labels = sorted(set(lab for lab, _ in rows))
    if len(labels) < 2:
        return None

    nf = len(FEATURES)
    confusion = {}
    correct = 0
    for i, (truth, vec) in enumerate(rows):
        train = [rows[j] for j in range(len(rows)) if j != i]
        mean = [sum(v[k] for _, v in train) / len(train) for k in range(nf)]
        std = []
        for k in range(nf):
            var = sum((v[k] - mean[k]) ** 2 for _, v in train) / len(train)
            std.append(math.sqrt(var))

        def z(vec_):
            return [0.0 if std[k] < 1e-9 else (vec_[k] - mean[k]) / std[k]
                    for k in range(nf)]

        cent = {}
        for lab in labels:
            members = [z(v) for l, v in train if l == lab]
            cent[lab] = [sum(m[k] for m in members) / len(members)
                         for k in range(nf)]
        zv = z(vec)
        pred = min(labels, key=lambda lab: sum(
            (zv[k] - cent[lab][k]) ** 2 for k in range(nf)))
        confusion[(truth, pred)] = confusion.get((truth, pred), 0) + 1
        if pred == truth:
            correct += 1
    return {
        "labels": labels,
        "n": len(rows),
        "accuracy": correct / len(rows),
        "confusion": confusion,
    }


# ---- terminal report ----

def render_terminal(walkups, cls, use_color):
    def paint(code, text):
        return "\033[%sm%s\033[0m" % (code, text) if use_color else text

    out = []
    out.append("Aliro Gait — E1 probe")
    analyzed = [w for w in walkups if w.features]
    if not walkups:
        out.append("no [ALAB] transactions found "
                   "(flash a lab build, then `lab on` at the console before the walk-up)")
        return "\n".join(out) + "\n"

    out.append("")
    out.append("  %-10s %4s %4s %6s %6s %4s %7s %7s %6s %5s %6s %5s %6s"
               % ("label", "txn", "n", "dur_s", "blk_ms", "ran", "cad_Hz",
                  "inc_Hz", "prom", "reg", "cm/s", "rms", "verdict"))
    for w in walkups:
        if w.skip:
            out.append("  %-10s %4d %4d  skipped: %s"
                       % (w.label, w.txn_index, w.n, w.skip))
            continue
        f = w.features
        verdict = "carry" if f["motion"] else "still"
        if f["motion"] and f["approach"]:
            verdict = "CARRY+"
        out.append("  %-10s %4d %4d %6.1f %6d %4d %7.2f %7s %6.1f %5.2f %6.1f %5.1f %6s"
                   % (w.label, w.txn_index, w.n, w.dur_s, w.block_ms, w.ran,
                      f["cadence_hz"],
                      "%.2f" % f["inc_cadence_hz"] if f["inc_cadence_hz"] else "-",
                      f["prominence"], f["regularity"], f["speed_cm_s"],
                      f["rms_cm"],
                      paint("32" if f["motion"] else "2", verdict)))
    out.append("")
    out.append("  verdicts: CARRY+ = carry motion and approaching, carry = "
               "carry motion, still = static channel; prom weights the "
               "cadence estimate (Tier 2), it does not gate the verdict")

    if cls:
        out.append("")
        pct = cls["accuracy"] * 100.0
        code = "32" if cls["accuracy"] >= 0.8 else "33"
        out.append("leave-one-out nearest-centroid: %s over %d walk-ups, %d carriers"
                   % (paint(code, "%.0f%%" % pct), cls["n"], len(cls["labels"])))
        for (truth, pred), n in sorted(cls["confusion"].items()):
            if truth != pred:
                out.append("  confused: %s -> %s x%d" % (truth, pred, n))
        out.append("  Tier-2 GO bar (pre-registered): >= 80% within one carry mode")
    elif len(set(w.label for w in analyzed)) >= 2:
        out.append("")
        out.append("classification skipped: need >= 2 analyzed walk-ups per label")
    return "\n".join(out) + "\n"


# ---- HTML report (same look as the Aliro Lab report) ----

_CSS = """
:root {
  color-scheme: light dark;
  --surface: #fcfcfb; --plane: #f9f9f7;
  --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --ring: rgba(11,11,11,0.10);
  --bar: #2a78d6; --track: #f0efec;
  --good: #0ca30c; --warning: #fab219; --critical: #d03b3b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --surface: #1a1a19; --plane: #0d0d0d;
    --ink: #ffffff; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --ring: rgba(255,255,255,0.10);
    --bar: #3987e5; --track: #26262a;
  }
}
* { box-sizing: border-box; margin: 0; }
body { background: var(--plane); color: var(--ink); padding: 2rem 1rem;
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 860px; margin: 0 auto; }
h1 { font-size: 1.35rem; margin-bottom: .25rem; }
.sub { color: var(--ink-2); margin-bottom: 1.5rem; }
section { background: var(--surface); border: 1px solid var(--ring);
  border-radius: 10px; padding: 1.25rem 1.5rem; margin-bottom: 1.25rem; }
h2 { font-size: 1.05rem; margin-bottom: .25rem; }
.meta { color: var(--ink-2); font-size: .9rem; margin-bottom: 1rem; }
table { border-collapse: collapse; width: 100%; margin-top: .35rem; }
th { text-align: left; color: var(--muted); font-size: .75rem;
  text-transform: uppercase; letter-spacing: .04em; padding: .3rem .5rem; }
td { padding: .3rem .5rem; border-top: 1px solid var(--grid);
  font-size: .9rem; font-variant-numeric: tabular-nums; }
td.skip { color: var(--muted); }
.badge { display: inline-block; font-size: .75rem; font-weight: 600;
  padding: .1rem .5rem; border-radius: 99px; border: 1px solid; }
.badge.walk { color: var(--good); border-color: var(--good); }
.badge.still { color: var(--muted); border-color: var(--muted); }
svg.scatter { width: 100%; height: auto; display: block; }
svg.scatter .grid { stroke: var(--grid); stroke-width: 1; }
svg.scatter .tick { fill: var(--muted); font-size: 11px; }
svg.scatter .axis { fill: var(--ink-2); font-size: 12px; }
.legend { color: var(--ink-2); font-size: .9rem; margin-top: .5rem; }
.legend b { font-weight: 600; }
.overall { font-size: 1.05rem; }
"""

_PALETTE = ["#2a78d6", "#0ca30c", "#fab219", "#d03b3b", "#8a5cd6", "#0aa3a3"]


def _scatter_svg(walkups):
    """Cadence vs stride regularity, one dot per walk-up, colored by label:
    the E1 eyeball plot (do the carriers cluster?)."""
    pts = [(w.features["cadence_hz"], w.features["regularity"], w.label)
           for w in walkups if w.features]
    if not pts:
        return ""
    labels = sorted(set(p[2] for p in pts))
    color = {lab: _PALETTE[i % len(_PALETTE)] for i, lab in enumerate(labels)}
    wpx, hpx, m = 640, 320, 42
    x_lo, x_hi = GAIT_BAND_LO_HZ, GAIT_BAND_HI_HZ
    y_lo, y_hi = 0.0, 1.0

    def sx(v):
        return m + (v - x_lo) / (x_hi - x_lo) * (wpx - 2 * m)

    def sy(v):
        return hpx - m - (v - y_lo) / (y_hi - y_lo) * (hpx - 2 * m)

    out = ['<svg class="scatter" viewBox="0 0 %d %d">' % (wpx, hpx)]
    for i in range(8):
        fx = x_lo + (x_hi - x_lo) * i / 7.0
        out.append('<line class="grid" x1="%.0f" y1="%d" x2="%.0f" y2="%d"/>'
                   % (sx(fx), m, sx(fx), hpx - m))
        out.append('<text class="tick" x="%.0f" y="%d" text-anchor="middle">%.1f</text>'
                   % (sx(fx), hpx - m + 16, fx))
    for i in range(5):
        fy = y_lo + (y_hi - y_lo) * i / 4.0
        out.append('<line class="grid" x1="%d" y1="%.0f" x2="%d" y2="%.0f"/>'
                   % (m, sy(fy), wpx - m, sy(fy)))
        out.append('<text class="tick" x="%d" y="%.0f" text-anchor="end">%.2f</text>'
                   % (m - 6, sy(fy) + 4, fy))
    out.append('<text class="axis" x="%d" y="%d" text-anchor="middle">cadence (Hz)</text>'
               % (wpx // 2, hpx - 4))
    out.append('<text class="axis" x="12" y="%d" transform="rotate(-90 12 %d)" '
               'text-anchor="middle">stride regularity</text>'
               % (hpx // 2, hpx // 2))
    for x, y, lab in pts:
        cx = min(max(sx(x), m), wpx - m)
        cy = min(max(sy(y), m), hpx - m)
        out.append('<circle cx="%.1f" cy="%.1f" r="5" fill="%s" fill-opacity="0.8">'
                   '<title>%s: %.2f Hz, reg %.2f</title></circle>'
                   % (cx, cy, color[lab], lab, x, y))
    out.append("</svg>")
    legend = " &nbsp; ".join(
        '<b style="color:%s">●</b> %s' % (color[lab], lab) for lab in labels)
    return "".join(out) + '<div class="legend">%s</div>' % legend


def render_html(walkups, cls, title):
    rows = []
    for w in walkups:
        if w.skip:
            rows.append("<tr><td>%s</td><td>%d</td>"
                        '<td class="skip" colspan="9">skipped: %s</td></tr>'
                        % (w.label, w.txn_index, w.skip))
            continue
        f = w.features
        badge = ('<span class="badge walk">CARRY%s</span>'
                 % ("+" if f["approach"] else "")) if f["motion"] \
            else '<span class="badge still">STILL</span>'
        rows.append(
            "<tr><td>%s</td><td>%d</td><td>%d</td><td>%.1f</td><td>%d</td>"
            "<td>%.2f</td><td>%.1f</td><td>%.2f</td><td>%.1f</td><td>%.1f</td>"
            "<td>%s</td></tr>"
            % (w.label, w.txn_index, w.n, w.dur_s, w.block_ms,
               f["cadence_hz"], f["prominence"], f["regularity"],
               f["speed_cm_s"], f["rms_cm"], badge))
    cls_html = ""
    if cls:
        conf = "".join("<tr><td>%s</td><td>%s</td><td>%d</td></tr>" % (t, p, n)
                       for (t, p), n in sorted(cls["confusion"].items()))
        cls_html = (
            "<section><h2>Leave-one-out classification</h2>"
            '<p class="overall">%.0f%% over %d walk-ups, %d carriers '
            "(Tier-2 GO bar: &ge; 80%%)</p>"
            "<table><tr><th>truth</th><th>predicted</th><th>n</th></tr>%s</table>"
            "</section>" % (cls["accuracy"] * 100.0, cls["n"],
                            len(cls["labels"]), conf))
    return (
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>%s</title><style>%s</style></head><body><main>"
        "<h1>Aliro Gait — E1 probe</h1><p class=\"sub\">%s</p>"
        "<section><h2>Walk-ups</h2><table>"
        "<tr><th>label</th><th>txn</th><th>n</th><th>dur s</th><th>blk ms</th>"
        "<th>cad Hz</th><th>prom</th><th>reg</th><th>cm/s</th><th>rms</th>"
        "<th>verdict</th></tr>%s</table></section>"
        "<section><h2>Cadence vs regularity</h2>%s</section>"
        "%s</main></body></html>"
        % (title, _CSS, title, "".join(rows), _scatter_svg(walkups), cls_html))


# ---- CLI ----

def main(argv):
    args = argv[1:]
    html_path = None
    labeled = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-h", "--help"):
            sys.stderr.write(__doc__)
            return 2
        if a == "-o":
            if i + 1 >= len(args):
                sys.stderr.write("aliro_gait: -o needs a path\n")
                return 2
            html_path = args[i + 1]
            i += 2
            continue
        if "=" in a and not os.path.exists(a):
            label, path = a.split("=", 1)
        else:
            label, path = os.path.splitext(os.path.basename(a))[0], a
        labeled.append((label, path))
        i += 1
    if not labeled:
        sys.stderr.write(__doc__)
        return 2

    try:
        walkups = load_walkups(labeled)
    except OSError as exc:
        sys.stderr.write("aliro_gait: %s\n" % exc)
        return 2

    cls = classify(walkups)
    sys.stdout.write(render_terminal(walkups, cls, sys.stdout.isatty()))
    if html_path:
        title = "Aliro Gait — %s" % ", ".join(
            os.path.basename(p) for _, p in labeled)
        with open(html_path, "w") as f:
            f.write(render_html(walkups, cls, title))
        sys.stdout.write("html report: %s\n" % html_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
