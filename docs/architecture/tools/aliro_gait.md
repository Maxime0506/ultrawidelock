<!-- generated documentation — edit the source, not this file -->
# `tools/aliro_gait.py`

Aliro Gait: carry-motion features from Aliro Lab walk-up captures.

Usage: python3 tools/aliro_gait.py [-o report.html] [label=]capture.log ...

E1 probe of the passive carry verification experiment: for every walk-up
transaction in the given "[ALAB]" captures, detrend the per-block
trusted-range series, FFT the residual, and report the carry-motion features
(cadence, stride regularity, approach speed, deceleration, closest approach,
residual RMS) plus a per-window carried/stationary verdict. With two or more
labels (one per carrier, e.g. alice=alice.log bob=bob.log) it also runs
leave-one-out nearest-centroid classification to measure whether the features
separate the carriers — the pre-registered Tier-2 GO bar is >= 80%.

The block duration (and the phone's implied RAN multiplier) is derived from
the range timestamps themselves, so no extra firmware logging is needed.
Exit status: 0 = report produced, 2 = usage/input error.

**depends on** [`tools/aliro_lab.py`](aliro_lab.md)  ·  **discussed in** [`docs/passive-carry-verification.md`](../../passive-carry-verification.md)

## API

### `class WalkUp`
`tools/aliro_gait.py:65`

One analyzed walk-up: identity, series shape, features (or skip reason).

**called by** `analyze_walkup`

#### `WalkUp.__init__(self, label, txn_index)`
`tools/aliro_gait.py:68`

Initialize one walk-up record: label and transaction index are fixed; n, dur_s, block_ms, ran, features, and skip are populated by analysis.

### `_median(xs)`
`tools/aliro_gait.py:82`

Return the median of xs (middle value if odd length, average of two middle values if even).

**called by** `_approach_windows`, `_polyfit2`, `analyze_walkup`

### `_solve3(a, b)`
`tools/aliro_gait.py:89`

Gaussian elimination for the 3x3 normal equations of the quadratic fit.

**called by** `_polyfit2`

### `_polyfit2(ts, xs)`
`tools/aliro_gait.py:106`

Least-squares x(t) = a*t^2 + b*t + c; returns (a, b, c).

**called by** `analyze_walkup`  ·  **calls** `_median`, `_solve3`

### `_resample(ts, xs, dt)`
`tools/aliro_gait.py:127`

Linear interpolation onto a uniform dt grid (missed blocks leave gaps
in the capture; the spectrum needs even spacing).

**called by** `analyze_walkup`

### `_hann(n)`
`tools/aliro_gait.py:148`

Return a Hann window of length n as a list; for n < 2 return all 1.0 (no window).

**called by** `_norm_spectrum`, `incremental_cadence_hz`

### `_dft_power(xs, nfft)`
`tools/aliro_gait.py:155`

Power spectrum, bins 0..nfft/2 (input zero-padded to nfft).
O(N*K) is fine at these sizes; keeps the tool stdlib-only.

**called by** `_norm_spectrum`

### `_norm_spectrum(xs, nfft)`
`tools/aliro_gait.py:170`

Hann-windowed magnitude spectrum normalized by coherent gain, so a
unit-amplitude sine at a bin center reads ~1.0 regardless of length.

**called by** `_diff_spectrum`  ·  **calls** `_dft_power`, `_hann`

### `_diff_spectrum(xs, fs, nfft)`
`tools/aliro_gait.py:179`

Spectrum of the first-differenced series, gain-compensated back to
amplitude units. Differencing crushes what the polynomial detrend leaves
below the gait band (slow multipath/body-shadow wander) — the dominant
source of false cadence peaks — and dividing by the difference filter's
gain |H| = 2 sin(pi f / fs) undoes its tilt so the peak location is
unbiased. DC (gain 0) is pinned to zero, which the band never reaches.

**called by** `_gait_spectra`  ·  **calls** `_norm_spectrum`

### `_gait_spectra(resid, fs, nfft)`
`tools/aliro_gait.py:195`

(coincidence, full) magnitude spectra of the residual. The coincidence
spectrum is the min() of the two half-window spectra: at ~26-sample
windows and SNR ~1 a single periodogram's noise lobes can out-peak the
true cadence line, but a noise lobe rarely lands on the same frequency in
both halves, while the gait line is in both by construction. The caller
picks the coarse peak on the min() and refines its location on the
full-window spectrum, which has the sharper mainlobe.

**called by** `analyze_walkup`  ·  **calls** `_diff_spectrum`

### `_goertzel_power(xs, f_hz, fs_hz)`
`tools/aliro_gait.py:211`

Compute the power at frequency f_hz in a signal xs sampled at fs_hz using the Goertzel algorithm: real-time single-pass O(n) equivalent to FFT bin power.

**called by** `incremental_cadence_hz`

### `incremental_cadence_hz(xs, fs_hz, band_lo, band_hi)`
`tools/aliro_gait.py:221`

Firmware-shaped estimator: first-difference high-pass (kills the
approach trend), Hann weight over the trailing window, Goertzel bank
across the gait band. This is what a ring buffer plus a few dozen
multiplies per block can afford on target; test_aliro_gait.py holds it
to the FFT answer so the future firmware learner mirrors this exactly.

**called by** `analyze_walkup`  ·  **calls** `_goertzel_power`, `_hann`

### `analyze_walkup(label, idx, ranges)`
`tools/aliro_gait.py:245`

Features from one approach's trusted-range series, already windowed to
the descending phase by _approach_windows.

**called by** `walkups_from_text`  ·  **calls** `WalkUp`, `_gait_spectra`, `_median`, `_polyfit2`, `_resample`, `incremental_cadence_hz`

### `_approach_windows(txn)`
`tools/aliro_gait.py:320`

Split one session into its individual approaches.

When the phone keeps the BLE session up while the carrier walks up several
times, one session.start..session.end holds multiple approaches, each ended
by a relock.sent. Split on those relocks and keep each approach's descending
phase (segment start .. closest approach), dropping the walk-away and the
door-side loitering that follow it. Legacy single-approach captures (no
relock, e.g. synthetic traces) fall back to the whole series, cut at the
bolt stamp as before.

**called by** `walkups_from_text`  ·  **calls** `_median`

### `walkups_from_text(label, text)`
`tools/aliro_gait.py:350`

Parse Aliro Lab event text into transactions, extract individual approaches (windowed ranges) from each, and return a list of WalkUp objects with features and skip reasons analyzed per approach.

**called by** `load_walkups`  ·  **calls** `_approach_windows`, `analyze_walkup`

### `load_walkups(labeled_paths)`
`tools/aliro_gait.py:361`

Load walk-up event sets from a list of labeled file paths. For each path, open the file and parse all transactions and approaches from its text, returning a list of WalkUp objects with features and skip reasons analyzed per approach.

**called by** `main`  ·  **calls** `walkups_from_text`

### `classify(walkups)`
`tools/aliro_gait.py:372`

Leave-one-out nearest-centroid over z-scored features. Returns None
unless there are >= 2 labels with >= 2 analyzed walk-ups each.

**called by** `main`  ·  **calls** `z`

### `z(vec_)`
`tools/aliro_gait.py:399`

Z-score a feature vector: compute mean and std of each feature across all samples, then return (vec[k] - mean[k]) / std[k] for each k (or 0.0 if std is negligible).

**called by** `classify`

### `render_terminal(walkups, cls, use_color)`
`tools/aliro_gait.py:425`

Render walk-up data as a terminal table with optional ANSI color. Columns: label, transaction index, event count, duration (s), block (ms), ranged-sample count, cadence (Hz), incident cadence (Hz), prominence, regularity, speed (cm/s), RMS (cm), verdict (CARRY+, carry, or still). Skipped walk-ups show skip reason. If classifier results provided, display leave-one-out accuracy and confusion matrix. Prom weights the cadence estimate (Tier 2); motion verdict combines cadence and approach detection.

**called by** `main`  ·  **calls** `paint`

### `paint(code, text)`
`tools/aliro_gait.py:427`

Return the string text wrapped in ANSI color code if use_color is true, otherwise return text unchanged.

**called by** `render_terminal`

### `_scatter_svg(walkups)`
`tools/aliro_gait.py:531`

Cadence vs stride regularity, one dot per walk-up, colored by label:
the E1 eyeball plot (do the carriers cluster?).

**called by** `render_html`  ·  **calls** `sx`, `sy`

### `sx(v)`
`tools/aliro_gait.py:544`

Map a value v on the x-axis (cadence_hz range) to SVG pixel coordinate m + (v - x_lo) / (x_hi - x_lo) * (wpx - 2*m), where m is margin and wpx is plot width.

**called by** `_scatter_svg`

### `sy(v)`
`tools/aliro_gait.py:548`

Map a value v on the y-axis (regularity range) to SVG pixel coordinate hpx - m - (v - y_lo) / (y_hi - y_lo) * (hpx - 2*m), where m is margin and hpx is plot height (inverted so higher values are higher on the plot).

**called by** `_scatter_svg`

### `render_html(walkups, cls, title)`
`tools/aliro_gait.py:582`

Render walk-ups as a standalone HTML document with a table of features (cadence, regularity, speed, RMS), a cadence-vs-regularity scatter plot, and (if classifier results provided) a leave-one-out confusion matrix. Title appears in <title> and <h1>.

**called by** `main`  ·  **calls** `_scatter_svg`

### `main(argv)`
`tools/aliro_gait.py:629`

Parse command-line arguments (label=path pairs or bare paths), load walk-ups from files, run leave-one-out nearest-centroid classifier (if >= 2 labels), and render terminal output (with ANSI color if stdout is a TTY). If -o is given, also render HTML report to that file. Return 2 on argument error, 0 on success, or OSError on file read failure.

**calls** `classify`, `load_walkups`, `render_html`, `render_terminal`
