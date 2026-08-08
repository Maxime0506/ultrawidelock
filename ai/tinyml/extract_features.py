#!/usr/bin/env python3
"""Extract LOS/NLOS features from the eWINE UWB data set.

Run:
    ai/tinyml/.venv/bin/python ai/tinyml/extract_features.py

Options (env vars):
    EWINE_DIR   dataset root (default ai/tinyml/data/UWB-LOS-NLOS-Data-Set-master)
    OUT         output .npz (default ai/tinyml/features.npz)
    WIN_PRE     CIR taps kept before the first-path index (default 15)
    WIN_POST    CIR taps kept after it (default 140)
    PORTABLE    1 = emit only features the DW3000/DW3110 can actually produce

PORTABLE matters. This data set is DW1000, whose diagnostics include STDEV_NOISE and
MAX_NOISE. The DW3000 `dwt_rxdiag_t` in modules/woz_dw3000/dwt_uwb_driver/deca_device_api.h has
no equivalent field, so stdev_noise, max_noise and the derived fp_snr cannot be computed
on this project's hardware. Training with them would produce a model that cannot be fed
on the target: the classic train/serve skew. PORTABLE=1 drops all three.

The firmware already captures CIR: modules/woz_uwb/src/driver/uwb_cirdiag.c reads
CIRDIAG_CIR_WIN = 64 Ipatov taps centred on the first-path index. To match that capture
exactly, use WIN_PRE=32 WIN_POST=32.

Two traps in the upstream data set, handled here:
  * its README normalises the CIR with `item[2]`, but column 2 is FP_IDX; RXPACC is
    column 9. Dividing by FP_IDX is wrong and this script divides by RXPACC.
  * its reference loader `code/uwb_dataset.py` calls `df.as_matrix()`, removed from
    pandas long ago, so it cannot run on a current environment at all.

Deliberately NOT used as features, to keep the model about the channel rather than
about this particular capture rig:
  RANGE       the measurement outcome, and strongly site-specific
  CH/BITRATE/PRFR   constant across the whole set (2 / 110 kbps / 64 MHz): zero information
  FRAME_LEN, PREAM_LEN   protocol/config values that vary with the capture setup, so a
              model can latch onto them as a site fingerprint instead of learning physics
"""

import os
import glob
import numpy as np
import pandas as pd

DATA = os.environ.get(
    "EWINE_DIR", "ai/tinyml/data/UWB-LOS-NLOS-Data-Set-master"
)
OUT = os.environ.get("OUT", "ai/tinyml/features.npz")
PORTABLE = os.environ.get("PORTABLE", "0") == "1"

# Features the DW3000/DW3110 cannot produce: its dwt_rxdiag_t carries no noise fields.
DW1000_ONLY = ("stdev_noise", "max_noise", "fp_snr")

# DW1000 power constant for a 64 MHz pulse repetition frequency (Decawave APS006 /
# DW1000 user manual section 4.7). The whole data set is PRF 64, asserted below.
A_CONST_PRF64 = 121.74

# CIR window relative to the reported first-path index, in 1 ns samples.
WIN_PRE = int(os.environ.get("WIN_PRE", "15"))
WIN_POST = int(os.environ.get("WIN_POST", "140"))

FEATURE_NAMES = [
    "fp_pwr",         # first-path power, dBm
    "rx_pwr",         # total received power, dBm
    "pwr_diff",       # rx_pwr - fp_pwr: the classic vendor NLOS discriminant
    "stdev_noise",
    "max_noise",
    "rxpacc",
    "fp_snr",         # strongest first-path amplitude over noise stdev
    "cir_energy",     # windowed, per-preamble-symbol
    "cir_max",
    "peak_over_fp",   # strongest tap over the first-path tap
    "peak_delay",     # samples between first path and strongest tap (ns)
    "mean_excess_delay",
    "rms_delay_spread",
    "kurtosis",
    "skewness",
    "rise_time",      # 10% -> 90% of peak, ns
    "late_early_ratio",
]


def load_raw():
    files = sorted(glob.glob(os.path.join(DATA, "dataset", "*.csv")))
    if not files:
        raise SystemExit(f"no CSVs under {DATA}/dataset")
    frames = [pd.read_csv(f) for f in files]
    df = pd.concat(frames, ignore_index=True)
    print(f"loaded {len(files)} files -> {df.shape[0]} samples, {df.shape[1]} columns")
    return df


def extract(df):
    cols = list(df.columns)
    assert cols[:15] == [
        "NLOS", "RANGE", "FP_IDX", "FP_AMP1", "FP_AMP2", "FP_AMP3",
        "STDEV_NOISE", "CIR_PWR", "MAX_NOISE", "RXPACC", "CH",
        "FRAME_LEN", "PREAM_LEN", "BITRATE", "PRFR",
    ], f"unexpected column layout: {cols[:15]}"
    assert cols[15] == "CIR0" and cols[-1] == "CIR1015", "unexpected CIR layout"

    y = df["NLOS"].to_numpy(dtype=np.int8)

    prfr = df["PRFR"].to_numpy()
    assert (prfr == 64).all(), "A constant below assumes PRF 64 MHz throughout"

    fp_idx = df["FP_IDX"].to_numpy(dtype=np.int32)
    fp1 = df["FP_AMP1"].to_numpy(dtype=np.float64)
    fp2 = df["FP_AMP2"].to_numpy(dtype=np.float64)
    fp3 = df["FP_AMP3"].to_numpy(dtype=np.float64)
    stdev_noise = df["STDEV_NOISE"].to_numpy(dtype=np.float64)
    cir_pwr = df["CIR_PWR"].to_numpy(dtype=np.float64)
    max_noise = df["MAX_NOISE"].to_numpy(dtype=np.float64)
    rxpacc = df["RXPACC"].to_numpy(dtype=np.float64)

    eps = 1e-9
    n_zero_pwr = int((cir_pwr <= 0).sum())
    if n_zero_pwr:
        print(f"note: {n_zero_pwr} samples have CIR_PWR == 0; clamped for the log")

    fp_pwr = 10.0 * np.log10((fp1**2 + fp2**2 + fp3**2) / rxpacc**2 + eps) - A_CONST_PRF64
    rx_pwr = 10.0 * np.log10(cir_pwr * (2.0**17) / rxpacc**2 + eps) - A_CONST_PRF64
    pwr_diff = rx_pwr - fp_pwr
    fp_snr = np.maximum.reduce([fp1, fp2, fp3]) / np.maximum(stdev_noise, eps)

    # CIR, normalised per acquired preamble symbol (README's intent, correct divisor).
    cir = df.iloc[:, 15:].to_numpy(dtype=np.float64) / rxpacc[:, None]
    n, ncir = cir.shape
    assert ncir == 1016, ncir

    lo = np.clip(fp_idx - WIN_PRE, 0, ncir - 1)
    win_len = WIN_PRE + WIN_POST
    idx = lo[:, None] + np.arange(win_len)[None, :]
    idx = np.clip(idx, 0, ncir - 1)
    w = np.take_along_axis(cir, idx, axis=1)          # |h| within the window
    t = np.arange(win_len, dtype=np.float64)          # ns, 1 ns per sample

    p = w**2                                          # power delay profile
    p_sum = p.sum(axis=1) + eps

    cir_energy = p_sum
    cir_max = w.max(axis=1)
    peak_i = w.argmax(axis=1).astype(np.float64)
    fp_tap = w[:, WIN_PRE]
    peak_over_fp = cir_max / np.maximum(fp_tap, eps)
    peak_delay = peak_i - WIN_PRE

    tau_m = (t[None, :] * p).sum(axis=1) / p_sum
    tau_rms = np.sqrt(((t[None, :] - tau_m[:, None]) ** 2 * p).sum(axis=1) / p_sum)

    mu = w.mean(axis=1)
    sd = w.std(axis=1) + eps
    z = (w - mu[:, None]) / sd[:, None]
    kurt = (z**4).mean(axis=1)
    skew = (z**3).mean(axis=1)

    # Rise time: first crossing of 90% of peak minus first crossing of 10% of peak.
    lo_thr = 0.1 * cir_max[:, None]
    hi_thr = 0.9 * cir_max[:, None]
    first_lo = (w >= lo_thr).argmax(axis=1).astype(np.float64)
    first_hi = (w >= hi_thr).argmax(axis=1).astype(np.float64)
    rise_time = first_hi - first_lo

    half = win_len // 2
    early = p[:, :half].sum(axis=1) + eps
    late = p[:, half:].sum(axis=1)
    late_early_ratio = late / early

    X = np.column_stack([
        fp_pwr, rx_pwr, pwr_diff, stdev_noise, max_noise, rxpacc, fp_snr,
        cir_energy, cir_max, peak_over_fp, peak_delay, tau_m, tau_rms,
        kurt, skew, rise_time, late_early_ratio,
    ]).astype(np.float32)

    assert X.shape[1] == len(FEATURE_NAMES), (X.shape, len(FEATURE_NAMES))
    bad = ~np.isfinite(X)
    if bad.any():
        print(f"note: {int(bad.sum())} non-finite feature cells zeroed")
        X[bad] = 0.0

    names = list(FEATURE_NAMES)
    if PORTABLE:
        keep = [i for i, n in enumerate(names) if n not in DW1000_ONLY]
        dropped = [n for n in names if n in DW1000_ONLY]
        X = X[:, keep]
        names = [names[i] for i in keep]
        print(f"PORTABLE: dropped {dropped} (no DW3000 dwt_rxdiag_t equivalent)")
    return X, y, names


def main():
    df = load_raw()
    X, y, names = extract(df)
    print(f"CIR window: {WIN_PRE} pre + {WIN_POST} post = {WIN_PRE + WIN_POST} taps")
    print(f"features {X.shape}, NLOS fraction {y.mean():.4f}")
    for i, name in enumerate(names):
        c = X[:, i]
        print(f"  {name:18} min {c.min():12.3f}  max {c.max():12.3f}  mean {c.mean():12.3f}")
    np.savez_compressed(OUT, X=X, y=y, names=np.array(names))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
