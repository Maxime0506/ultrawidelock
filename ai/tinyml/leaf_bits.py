#!/usr/bin/env python3
"""Does emlearn's leaf_bits actually close the forest-vs-sklearn gap?

Run from the repository root:
    ai/tinyml/.venv/bin/python <this file>

Result 5 recorded that a converted RandomForest disagrees with sklearn on ~1% of
samples, because the generated C majority-votes where sklearn averages
probabilities, and concluded a forest has no oracle to be certified against.
emlearn 0.23.2's trees.py:553 switches from forest_predict_majority_func to
forest_predict_proportions_func whenever leaf_bits != 0, and leaf_bits defaults
to 0 for classifiers. So the divergence may be a default rather than a property.

It trades one error for another: quantize_probabilities_into_byte (trees.py:26)
puts each leaf's probabilities on 255 steps, so near-ties can still flip. Which
error is bigger is measurable, and this measures it on both data sets that
matter -- the 544 door captures the shipped model uses, and the 42,000-sample
public set the bake-off ran on.
"""

import os
import sys

import numpy as np
import emlearn
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split

sys.path.insert(0, "ai/tinyml")
from features_io import read_features
from gen_model import emlearn_scratch, quantise_f64

SEED = 42
SETS = [
    ("door captures", "ai/tinyml/captures-door-2026-08-07-ranged.csv",
     ["fp_resid", "rx_pwr"]),
    ("public eWINE", "ai/tinyml/features_dw3000.npz",
     ["fp_pwr", "rx_pwr", "pwr_diff", "rxpacc"]),
]


def run(name, path, wanted, n_trees=20):
    if not os.path.exists(path):
        print(f"{name}: {path} absent, skipped")
        return
    X, y, names, _ = read_features(path)
    keep = [names.index(n) for n in wanted if n in names]
    if len(keep) != len(wanted):
        print(f"{name}: wanted features missing, skipped")
        return
    X = X[:, keep]

    Xtr, Xtmp, ytr, ytmp = train_test_split(X, y, test_size=0.30, random_state=SEED,
                                            stratify=y)
    _, Xte, _, yte = train_test_split(Xtmp, ytmp, test_size=0.50, random_state=SEED,
                                      stratify=ytmp)
    lo = Xtr.min(axis=0)
    scale = 30000.0 / np.maximum(Xtr.max(axis=0) - lo, 1e-9)
    Qtr, Qte = quantise_f64(Xtr, lo, scale), quantise_f64(Xte, lo, scale)

    m = RandomForestClassifier(n_estimators=n_trees, max_depth=6,
                               random_state=SEED).fit(Qtr, ytr)
    sk = m.predict(Qte)

    print(f"\n{name}: {len(yte)} held-out samples, {n_trees} trees, depth 6")
    print(f"  {'leaf_bits':>10}  {'mismatches':>10}  {'rate':>8}")
    for lb in (0, 8, 6, 4):
        kw = {} if lb == 0 else {"leaf_bits": lb}
        try:
            cm = emlearn.convert(m, method="inline", dtype="int16_t", **kw)
            with emlearn_scratch():
                pred = cm.predict(Qte)
        except Exception as e:  # noqa: BLE001 - report, do not hide
            print(f"  {lb:>10}  conversion failed: {type(e).__name__}: {e}")
            continue
        bad = int((pred != sk).sum())
        tag = "  <- emlearn default for a classifier" if lb == 0 else ""
        print(f"  {lb:>10}  {bad:>10}  {bad / len(yte):>7.2%}{tag}")


if __name__ == "__main__":
    for name, path, wanted in SETS:
        run(name, path, wanted)
