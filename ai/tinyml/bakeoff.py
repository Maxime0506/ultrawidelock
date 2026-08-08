#!/usr/bin/env python3
"""LOS/NLOS bake-off: emlearn decision trees vs an int8-quantised Keras MLP.

Run (after extract_features.py):
    ai/tinyml/.venv/bin/python ai/tinyml/bakeoff.py

Options (env vars):
    FEATURES    input .npz (default ai/tinyml/features.npz)
    OUTDIR      where generated C / .tflite land (default ai/tinyml/out)
    SEED        split + init seed (default 42)

What "bytes" means here. Only the model payload is counted, because that is the part
this script can measure exactly and architecture-independently:
  * trees   n_nodes*8 (EmlTreesNode = int8+3*int16, 8 B aligned) + n_roots*4 + n_leaves
  * MLP     len(tflite flatbuffer)
  * both    + 136 B of feature scaling constants (17 features * 2 float32)
Runtime code size (TFLM interpreter + kernels, vs emlearn's eml_trees inference) is NOT
measured here: it needs a cross-compile for the target and no ARM toolchain is installed
in this worktree. Literature puts TFLM's interpreter core near 2 KB before kernels and
emlearn's tree inference in the low hundreds of bytes, which moves the comparison further
in the trees' favour, not less.
"""

import contextlib
import os
import re
import json
import tempfile

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "3")

from sklearn.tree import DecisionTreeClassifier
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split

import emlearn

FEATURES = os.environ.get("FEATURES", "ai/tinyml/features.npz")
OUTDIR = os.environ.get("OUTDIR", "ai/tinyml/out")
SEED = int(os.environ.get("SEED", "42"))
SUBSET = os.environ.get("SUBSET", "all")

# SUBSET=shape keeps only features that describe the SHAPE of the channel response and
# discards every absolute-power term. Motivation: in this data set LOS and NLOS captures
# span many ranges, so "weak signal" correlates with NLOS for reasons that have nothing to
# do with the obstruction. A door reader sees a roughly fixed geometry, where that shortcut
# is unavailable. pwr_diff survives because it is a dB ratio, not an absolute level.
SHAPE_FEATURES = [
    "pwr_diff", "peak_over_fp", "peak_delay", "mean_excess_delay",
    "rms_delay_spread", "kurtosis", "skewness", "rise_time", "late_early_ratio",
]

# SUBSET=scalar keeps only what dwt_readdiagnostics returns without touching the CIR
# accumulator: all four derive from ipatovPower, ipatovF1..F3 and ipatovAccumCount alone.
# This is the decision-relevant subset. Reading the accumulator needs the window dump, and
# as of 2026-08-07 the cirdiag image stops the DWM3001CDK responder transmitting at all
# (tx0, no range, no unlock), while the plain image ranges normally. If accuracy holds up
# here, the door captures need only the cheap summary path and never the accumulator.
SCALAR_FEATURES = ["fp_pwr", "rx_pwr", "pwr_diff", "rxpacc"]

# SUBSET=scalar+ adds three the registers also carry: ipatovPeak packs the strongest tap's
# index and amplitude, so peak height and its offset from the first path are readable
# without the accumulator. OPTIMISTIC -- eWINE computes these three from the taps, and a
# register-derived version would need its own derivation to match numerically. Read it as
# an upper bound on what scalars can reach, not as a measured firmware capability.
SCALAR_PLUS_FEATURES = SCALAR_FEATURES + ["cir_max", "peak_delay", "peak_over_fp"]

SUBSETS = {
    "shape": SHAPE_FEATURES,
    "scalar": SCALAR_FEATURES,
    "scalar+": SCALAR_PLUS_FEATURES,
}

SCALE_CONST_BYTES = 0  # set from the actual feature count in main()


@contextlib.contextmanager
def emlearn_scratch():
    """Run emlearn's compile-and-load from a throwaway directory.

    `predict()` builds its Python-callable extension at the fixed path
    `./tmp/mytree.{c,h,o}` -- `name = 'mytree'` in emlearn/trees.py under
    `temp_dir='tmp'` in emlearn/common.py -- no matter what name `save()` was
    given. From the repository root that leaves build spill in the tree, and two
    concurrent runs sharing a directory overwrite each other's extension so one
    silently loads the other's. That failure is silent and looks exactly like a
    model bug: it once reported 6300/6300 mismatches that were nothing of the
    kind. A private directory per process removes both problems.
    """
    cwd = os.getcwd()
    with tempfile.TemporaryDirectory(prefix="emlearn-") as d:
        os.chdir(d)
        try:
            yield
        finally:
            os.chdir(cwd)


def quantise_int16(X, lo, span):
    """Affine per-feature map into int16. Trees are trained on the result, so the
    emlearn int16 conversion is lossless rather than a post-hoc approximation."""
    q = (X - lo) * (32000.0 / span) - 16000.0
    return np.clip(np.round(q), -32768, 32767).astype(np.int16)


def c_model_bytes(path):
    """Exact payload bytes from the emlearn-generated C."""
    src = open(path).read()
    def arr(name, elem):
        m = re.search(rf"{name}\[(\d+)\]", src)
        return (int(m.group(1)) if m else 0), (int(m.group(1)) if m else 0) * elem
    n_nodes, b_nodes = arr(r"\w*_nodes", 8)
    n_roots, b_roots = arr(r"\w*_tree_roots", 4)
    n_leaves, b_leaves = arr(r"\w*_leaves", 1)
    return {
        "n_nodes": n_nodes, "n_roots": n_roots, "n_leaves": n_leaves,
        "bytes": b_nodes + b_roots + b_leaves + SCALE_CONST_BYTES,
    }


def eval_trees(name, model, Xtr, ytr, Xte, yte, results):
    model.fit(Xtr, ytr)
    acc = float(model.score(Xte, yte))
    cm = emlearn.convert(model, method="loadable", dtype="int16_t")
    path = os.path.join(OUTDIR, f"{name}.h")
    cm.save(name=name.replace("-", "_"), file=path)
    info = c_model_bytes(path)
    with emlearn_scratch():
        c_pred = cm.predict(Xte)
    c_acc = float((c_pred == yte).mean())
    mismatch = int((c_pred != model.predict(Xte)).sum())
    results.append({
        "model": name, "acc": acc, "c_acc": c_acc, "c_mismatch": mismatch,
        "bytes": info["bytes"], "nodes": info["n_nodes"], "kind": "tree",
    })
    print(f"  {name:28} acc {acc:.4f}  C {c_acc:.4f}  nodes {info['n_nodes']:6}  "
          f"{info['bytes']:7} B  (C/sklearn mismatches {mismatch})")


def build_mlp(hidden, n_in, seed):
    import tensorflow as tf
    tf.keras.utils.set_random_seed(seed)
    layers = [tf.keras.layers.Input(shape=(n_in,))]
    for h in hidden:
        layers.append(tf.keras.layers.Dense(h, activation="relu"))
    layers.append(tf.keras.layers.Dense(1, activation="sigmoid"))
    m = tf.keras.Sequential(layers)
    m.compile(optimizer="adam", loss="binary_crossentropy", metrics=["accuracy"])
    return m


def eval_mlp(name, hidden, Xtr, ytr, Xva, yva, Xte, yte, results):
    import tensorflow as tf
    m = build_mlp(hidden, Xtr.shape[1], SEED)
    m.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=80, batch_size=256, verbose=0,
          callbacks=[tf.keras.callbacks.EarlyStopping(patience=10,
                                                      restore_best_weights=True)])
    f32_acc = float(((m.predict(Xte, verbose=0)[:, 0] > 0.5).astype(np.int8) == yte).mean())

    export_dir = os.path.join(OUTDIR, f"{name}_saved")
    m.export(export_dir)
    conv = tf.lite.TFLiteConverter.from_saved_model(export_dir)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    rep = Xtr[:512].astype(np.float32)
    conv.representative_dataset = lambda: ([x.reshape(1, -1)] for x in rep)
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    blob = conv.convert()
    path = os.path.join(OUTDIR, f"{name}.tflite")
    open(path, "wb").write(blob)

    it = tf.lite.Interpreter(model_content=blob)
    it.allocate_tensors()
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    si, zi = inp["quantization"]
    so, zo = out["quantization"]
    preds = np.empty(len(Xte), dtype=np.int8)
    for i, x in enumerate(Xte.astype(np.float32)):
        q = np.clip(np.round(x / si + zi), -128, 127).astype(np.int8).reshape(1, -1)
        it.set_tensor(inp["index"], q)
        it.invoke()
        r = it.get_tensor(out["index"])[0, 0]
        preds[i] = 1 if (float(r) - zo) * so > 0.5 else 0
    int8_acc = float((preds == yte).mean())
    total = len(blob) + SCALE_CONST_BYTES
    results.append({
        "model": name, "acc": int8_acc, "f32_acc": f32_acc, "bytes": total,
        "flatbuffer": len(blob), "kind": "mlp",
    })
    print(f"  {name:28} int8 {int8_acc:.4f}  f32 {f32_acc:.4f}  "
          f"flatbuffer {len(blob):6} B  total {total:7} B")


def main():
    global SCALE_CONST_BYTES
    os.makedirs(OUTDIR, exist_ok=True)
    d = np.load(FEATURES, allow_pickle=True)
    X, y, names = d["X"].astype(np.float64), d["y"], list(d["names"])

    if SUBSET in SUBSETS:
        wanted = SUBSETS[SUBSET]
        keep = [i for i, n in enumerate(names) if n in wanted]
        missing = [n for n in wanted if n not in names]
        if missing:
            raise SystemExit(f"SUBSET={SUBSET}: {missing} absent from {FEATURES}")
        X = X[:, keep]
        names = [names[i] for i in keep]
        print(f"SUBSET={SUBSET}: kept {names}")
    elif SUBSET != "all":
        raise SystemExit(f"unknown SUBSET={SUBSET}; pick one of all/{'/'.join(SUBSETS)}")

    SCALE_CONST_BYTES = X.shape[1] * 2 * 4
    print(f"{X.shape[0]} samples, {X.shape[1]} features, NLOS fraction {y.mean():.4f}")
    print("NOTE: the eWINE files are pre-shuffled across all 7 locations and carry no")
    print("      location column, so a held-out split is RANDOM, not leave-one-site-out.")
    print("      These accuracies do not evidence cross-environment generalisation.\n")

    Xtr, Xtmp, ytr, ytmp = train_test_split(X, y, test_size=0.30, random_state=SEED,
                                            stratify=y)
    Xva, Xte, yva, yte = train_test_split(Xtmp, ytmp, test_size=0.50, random_state=SEED,
                                          stratify=ytmp)
    print(f"split train {len(ytr)} / val {len(yva)} / test {len(yte)}\n")

    results = []

    # --- baseline 0: the vendor rule of thumb, zero model bytes -------------------
    pd_i = names.index("pwr_diff")
    ths = np.linspace(0, 15, 301)
    accs = [((Xtr[:, pd_i] > t).astype(np.int8) == ytr).mean() for t in ths]
    best_t = float(ths[int(np.argmax(accs))])
    base_acc = float(((Xte[:, pd_i] > best_t).astype(np.int8) == yte).mean())
    six_acc = float(((Xte[:, pd_i] > 6.0).astype(np.int8) == yte).mean())
    print("baselines")
    print(f"  {'pwr_diff > 6 dB (vendor)':28} acc {six_acc:.4f}  0 B")
    print(f"  {'pwr_diff > %.2f dB (tuned)' % best_t:28} acc {base_acc:.4f}  8 B")
    results.append({"model": "pwr_diff > 6 dB (vendor rule)", "acc": six_acc,
                    "bytes": 0, "kind": "rule"})
    results.append({"model": f"pwr_diff > {best_t:.2f} dB (tuned)", "acc": base_acc,
                    "bytes": 8, "kind": "rule"})

    # --- trees on int16-quantised features ---------------------------------------
    lo = Xtr.min(axis=0)
    span = np.maximum(Xtr.max(axis=0) - lo, 1e-9)
    Qtr, Qva, Qte = (quantise_int16(a, lo, span) for a in (Xtr, Xva, Xte))

    print("\ntrees (emlearn, int16 features, C verified against sklearn)")
    for depth in (4, 6, 8, 10, 12):
        eval_trees(f"dtree-d{depth}", DecisionTreeClassifier(max_depth=depth,
                   random_state=SEED), Qtr, ytr, Qte, yte, results)
    for n in (5, 10, 20):
        for depth in (6, 8, 10):
            eval_trees(f"rf-n{n}-d{depth}",
                       RandomForestClassifier(n_estimators=n, max_depth=depth,
                                              random_state=SEED, n_jobs=-1),
                       Qtr, ytr, Qte, yte, results)

    # --- MLPs on standardised features -------------------------------------------
    mu, sd = Xtr.mean(axis=0), Xtr.std(axis=0) + 1e-9
    Str, Sva, Ste = ((a - mu) / sd for a in (Xtr, Xva, Xte))
    print("\nMLP (Keras -> int8 full-integer PTQ)")
    for hidden in ([8], [16, 8], [32, 16]):
        eval_mlp("mlp-" + "x".join(map(str, hidden)), hidden,
                 Str, ytr, Sva, yva, Ste, yte, results)

    # --- feature importance -------------------------------------------------------
    rf = RandomForestClassifier(n_estimators=50, max_depth=10, random_state=SEED,
                                n_jobs=-1).fit(Qtr, ytr)
    order = np.argsort(rf.feature_importances_)[::-1]
    print("\nfeature importance (RF-50-d10)")
    for i in order:
        print(f"  {names[i]:20} {rf.feature_importances_[i]:.4f}")

    with open(os.path.join(OUTDIR, "results.json"), "w") as f:
        json.dump({"results": results,
                   "features": names,
                   "importance": {names[i]: float(rf.feature_importances_[i])
                                  for i in order}}, f, indent=2)

    print("\n| model | test acc | payload bytes | acc per KB |")
    print("|---|---|---|---|")
    for r in sorted(results, key=lambda r: -r["acc"]):
        per = "-" if r["bytes"] == 0 else f"{r['acc'] / (r['bytes'] / 1024.0):.2f}"
        print(f"| {r['model']} | {r['acc']:.4f} | {r['bytes']} | {per} |")


if __name__ == "__main__":
    main()
