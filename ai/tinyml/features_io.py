#!/usr/bin/env python3
"""Read and write labelled feature sets, as `.npz` or as `.csv`.

Both formats carry the same four arrays: `X` (n x f float64), `y` (n int labels,
0 clear / 1 blocked), `names` (f feature names) and `frame_len` (n int, the RFRAME
length each reception came from, kept so a set can be re-filtered without
re-parsing the logs).

CSV EXISTS BECAUSE OF THE `mal-diff` GATE, not for convenience. `security/` blocks
any binary blob entering the tree, on the grounds that semgrep cannot parse it and
a reviewer cannot read it, and that is the correct call. But the captures the
shipped model is trained on are NOT regenerable from a public download the way the
eWINE set is: their only other copy is capture logs on one laptop. So the set that
`modules/woz_ml/` is fitted on lives in the tree as text, and stays reviewable.

The `.npz` path is still the default for the eWINE-derived sets, which are large,
regenerable, and gitignored.
"""

import csv

import numpy as np


def write_features(path, X, y, frame_len, names):
    """Write to `path`, choosing the format from its extension."""
    if str(path).endswith(".csv"):
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(list(names) + ["label", "frame_len"])
            for row, label, ln in zip(X, y, frame_len):
                # 12 significant figures, which is far past the 0.1 dB the
                # registers resolve and past float32's 7.2 digits, because this
                # file is the tracked provenance for shipped constants: the
                # generated scaler must come out bit-identical whether it was fitted
                # from this CSV or from the float64 arrays it was written from.
                # At 6 digits it did not -- the constants moved in their 7th figure,
                # harmless for the model and still the wrong property for a
                # provenance file to have.
                w.writerow([f"{v:.12g}" for v in row] + [int(label), int(ln)])
        return
    np.savez(path, X=X, y=y, names=np.array(names), frame_len=frame_len)


def read_features(path):
    """Return `(X, y, names, frame_len)` from a `.npz` or `.csv` written above."""
    if str(path).endswith(".csv"):
        with open(path, newline="") as f:
            rows = list(csv.reader(f))
        names = rows[0][:-2]
        body = np.array(rows[1:], dtype=np.float64)
        return (body[:, :len(names)], body[:, -2].astype(np.int64), names,
                body[:, -1].astype(np.int64))
    # allow_pickle stays off: `names` is written as a fixed-width unicode array,
    # so nothing here needs the pickle machinery and no tracked file should.
    d = np.load(path)
    return (d["X"].astype(np.float64), d["y"], [str(n) for n in d["names"]],
            d["frame_len"] if "frame_len" in d.files else None)
