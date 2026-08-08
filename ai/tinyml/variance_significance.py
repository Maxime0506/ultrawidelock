#!/usr/bin/env python3
"""Is the best variance gain real, and is the information already in the model?

Run from the repository root, after ai/tinyml/variance.py:
    ai/tinyml/.venv/bin/python ai/tinyml/variance_significance.py

Two questions the headline deltas cannot answer.

1. SIGNIFICANCE. +0.028 on 8 folds is one or two receptions per fold. A paired
   bootstrap over whole blocks -- resampling the unit that was held out, not the
   rows -- says how often the gain survives a different draw of the same data.

2. REDUNDANCY. pwr_diff = rx_pwr - fp_pwr, and both are already in the model, so
   a large solo effect size does not imply new information. If its variance is
   predictable from the shipped pair, the model already has it.
"""

import csv
import numpy as np
from sklearn.metrics import balanced_accuracy_score
from sklearn.model_selection import GroupKFold
from sklearn.tree import DecisionTreeClassifier

CSV = "ai/tinyml/captures-door-2026-08-07-ranged.csv"
SEED = 42
N_BOOT = 2000
N_BLOCKS = 8


def rolling_std(x, w):
    out = np.full(x.shape, np.nan)
    for i in range(w - 1, len(x)):
        out[i] = np.std(x[i - w + 1:i + 1], ddof=1)
    return out


def main():
    rows = list(csv.DictReader(open(CSV)))
    g = lambda c: np.array([float(r[c]) for r in rows])
    y = np.array([int(r["label"]) for r in rows])
    fp_resid, rx_pwr, pwr_diff = g("fp_resid"), g("rx_pwr"), g("pwr_diff")

    groups = np.zeros(len(y), dtype=int)
    k = 0
    for L in (0, 1):
        for chunk in np.array_split(np.flatnonzero(y == L), N_BLOCKS):
            groups[chunk] = k
            k += 1

    w = 5
    v = rolling_std(pwr_diff, w)
    ok = ~np.isnan(v)
    for i in range(1, w):
        ok[:i] = False
        ok[i:] &= groups[i:] == groups[:-i]

    base = np.column_stack([fp_resid, rx_pwr])[ok]
    aug = np.column_stack([fp_resid[ok], rx_pwr[ok], v[ok]])
    yy, gg = y[ok], groups[ok]

    def per_row_correct(X):
        pred = np.zeros(len(yy))
        for tr, te in GroupKFold(n_splits=8).split(X, yy, gg):
            m = DecisionTreeClassifier(max_depth=4, random_state=SEED).fit(X[tr], yy[tr])
            pred[te] = m.predict(X[te])
        return pred

    p_base, p_aug = per_row_correct(base), per_row_correct(aug)
    b0 = balanced_accuracy_score(yy, p_base)
    b1 = balanced_accuracy_score(yy, p_aug)
    print(f"depth-4, {int(ok.sum())} rows: base {b0:.4f}  +pwr_diff.std5 {b1:.4f}  "
          f"delta {b1 - b0:+.4f}\n")

    # --- paired bootstrap over BLOCKS, the unit that was held out -----------
    rng = np.random.RandomState(SEED)
    blocks = np.unique(gg)
    wins = 0
    deltas = np.empty(N_BOOT)
    for i in range(N_BOOT):
        pick = rng.choice(blocks, size=len(blocks), replace=True)
        idx = np.concatenate([np.flatnonzero(gg == b) for b in pick])
        d = (balanced_accuracy_score(yy[idx], p_aug[idx])
             - balanced_accuracy_score(yy[idx], p_base[idx]))
        deltas[i] = d
        wins += d > 0
    lo, hi = np.percentile(deltas, [2.5, 97.5])
    print(f"paired bootstrap over {len(blocks)} blocks, {N_BOOT} draws:")
    print(f"  P(variance feature is better) = {wins / N_BOOT:.3f}")
    print(f"  95% CI on the delta = [{lo:+.4f}, {hi:+.4f}]")
    print(f"  -> {'CI excludes zero' if lo > 0 else 'CI INCLUDES ZERO: not distinguishable from noise'}\n")

    # --- redundancy: is the variance predictable from what ships? ----------
    from sklearn.ensemble import RandomForestRegressor
    from sklearn.metrics import r2_score
    r2 = []
    for tr, te in GroupKFold(n_splits=8).split(base, yy, gg):
        m = RandomForestRegressor(n_estimators=200, random_state=SEED, n_jobs=-1)
        m.fit(base[tr], v[ok][tr])
        r2.append(r2_score(v[ok][te], m.predict(base[te])))
    print(f"redundancy: predicting pwr_diff.std5 from [fp_resid, rx_pwr] alone")
    print(f"  out-of-block R^2 = {np.mean(r2):.3f}")
    print("  (high R^2 means the shipped pair already encodes it)")


if __name__ == "__main__":
    main()
