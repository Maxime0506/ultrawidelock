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

### `_solve3(a, b)`
`tools/aliro_gait.py:87`

Gaussian elimination for the 3x3 normal equations of the quadratic fit.

**called by** `_polyfit2`

### `_polyfit2(ts, xs)`
`tools/aliro_gait.py:104`

Least-squares x(t) = a*t^2 + b*t + c; returns (a, b, c).

**called by** `analyze_walkup`  ·  **calls** `_median`, `_solve3`

### `_resample(ts, xs, dt)`
`tools/aliro_gait.py:125`

Linear interpolation onto a uniform dt grid (missed blocks leave gaps
in the capture; the spectrum needs even spacing).

**called by** `analyze_walkup`

### `_dft_power(xs, nfft)`
`tools/aliro_gait.py:152`

Power spectrum, bins 0..nfft/2 (input zero-padded to nfft).
O(N*K) is fine at these sizes; keeps the tool stdlib-only.

**called by** `_norm_spectrum`

### `_norm_spectrum(xs, nfft)`
`tools/aliro_gait.py:167`

Hann-windowed magnitude spectrum normalized by coherent gain, so a
unit-amplitude sine at a bin center reads ~1.0 regardless of length.

**called by** `_diff_spectrum`  ·  **calls** `_dft_power`, `_hann`

### `_diff_spectrum(xs, fs, nfft)`
`tools/aliro_gait.py:176`

Spectrum of the first-differenced series, gain-compensated back to
amplitude units. Differencing crushes what the polynomial detrend leaves
below the gait band (slow multipath/body-shadow wander) — the dominant
source of false cadence peaks — and dividing by the difference filter's
gain |H| = 2 sin(pi f / fs) undoes its tilt so the peak location is
unbiased. DC (gain 0) is pinned to zero, which the band never reaches.

**called by** `_gait_spectra`  ·  **calls** `_norm_spectrum`

### `_gait_spectra(resid, fs, nfft)`
`tools/aliro_gait.py:192`

(coincidence, full) magnitude spectra of the residual. The coincidence
spectrum is the min() of the two half-window spectra: at ~26-sample
windows and SNR ~1 a single periodogram's noise lobes can out-peak the
true cadence line, but a noise lobe rarely lands on the same frequency in
both halves, while the gait line is in both by construction. The caller
picks the coarse peak on the min() and refines its location on the
full-window spectrum, which has the sharper mainlobe.

**called by** `analyze_walkup`  ·  **calls** `_diff_spectrum`

### `incremental_cadence_hz(xs, fs_hz, band_lo, band_hi)`
`tools/aliro_gait.py:217`

Firmware-shaped estimator: first-difference high-pass (kills the
approach trend), Hann weight over the trailing window, Goertzel bank
across the gait band. This is what a ring buffer plus a few dozen
multiplies per block can afford on target; test_aliro_gait.py holds it
to the FFT answer so the future firmware learner mirrors this exactly.

**called by** `analyze_walkup`  ·  **calls** `_goertzel_power`, `_hann`

### `analyze_walkup(label, idx, ranges)`
`tools/aliro_gait.py:241`

Features from one approach's trusted-range series, already windowed to
the descending phase by _approach_windows.

**called by** `walkups_from_text`  ·  **calls** `WalkUp`, `_gait_spectra`, `_median`, `_polyfit2`, `_resample`, `incremental_cadence_hz`

### `_approach_windows(txn)`
`tools/aliro_gait.py:316`

Split one session into its individual approaches.

When the phone keeps the BLE session up while the carrier walks up several
times, one session.start..session.end holds multiple approaches, each ended
by a relock.sent. Split on those relocks and keep each approach's descending
phase (segment start .. closest approach), dropping the walk-away and the
door-side loitering that follow it. Legacy single-approach captures (no
relock, e.g. synthetic traces) fall back to the whole series, cut at the
bolt stamp as before.

**called by** `walkups_from_text`  ·  **calls** `_median`

### `classify(walkups)`
`tools/aliro_gait.py:366`

Leave-one-out nearest-centroid over z-scored features. Returns None
unless there are >= 2 labels with >= 2 analyzed walk-ups each.

**called by** `main`  ·  **calls** `z`

### `_scatter_svg(walkups)`
`tools/aliro_gait.py:522`

Cadence vs stride regularity, one dot per walk-up, colored by label:
the E1 eyeball plot (do the carriers cluster?).

**called by** `render_html`  ·  **calls** `sx`, `sy`

<details><summary>Undocumented (13)</summary>

- `WalkUp.__init__`
- `_median`
- `_hann`
- `_goertzel_power`
- `walkups_from_text`
- `load_walkups`
- `z`
- `render_terminal`
- `paint`
- `sx`
- `sy`
- `render_html`
- `main`

</details>
