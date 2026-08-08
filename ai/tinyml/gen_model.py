#!/usr/bin/env python3
"""Generate the shipped LOS/NLOS tree, its scaler, and the golden vectors.

Run (after extract_features.py with PORTABLE=1):
    ai/tinyml/.venv/bin/python ai/tinyml/gen_model.py

Options (env vars):
    FEATURES    input .npz (default ai/tinyml/features_dw3000.npz)
    SEED        split seed, must match bakeoff.py (default 42)
    DEPTH       tree depth (default 4; see RESULTS.md for why not deeper)
    VECTORS     how many golden vectors to emit (default 256)

Writes four generated files, none of them hand-editable:

    modules/woz_ml/include/woz_ml_los_features.h  feature count and order: the
                                                  public half, safe to include
                                                  from anywhere
    modules/woz_ml/src/woz_ml_los_scaler.h        the float32 scaling constants,
                                                  private because they are
                                                  `static const` arrays and a
                                                  public header would copy them
                                                  into every including TU
    modules/woz_ml/src/woz_ml_los_tree.h          emlearn `inline` C for the tree
    tests/host/data/woz_ml_los_vectors.h          golden vectors, sklearn-labelled

and refuses to write any of them unless two checks pass:

  1. The generated C reproduces sklearn on all 6,300 held-out samples. Result 5
     in RESULTS.md is why this is a hard gate for a single tree and still not one
     for a forest -- though Result 14 corrects the reason: emlearn's leaf_bits
     defaults to majority voting for classifiers, and leaf_bits=8 takes a forest
     from 0.92% disagreement to 0.06%. Close, and 0.06% is not zero.
  2. The float32 scaler the firmware runs agrees with the float64 one sklearn was
     trained through, on every held-out sample. A feature landing on a rounding
     boundary would otherwise classify differently on target than on the bench,
     which is train/serve skew that no amount of accuracy reporting would catch.

The golden vectors are stratified over the tree's LEAVES rather than sampled at
random, so every decision path in the shipped model is exercised. Random sampling
would over-cover the fat leaves and might never reach a thin one.
"""

import contextlib
import os
import re
import shutil
import subprocess
import tempfile

import numpy as np

from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import train_test_split

import emlearn

from features_io import read_features

# EVERY DEFAULT HERE IS THE SHIPPED MODEL, so a bare run of this script reproduces the
# headers in modules/woz_ml/ and nothing else. That is not tidiness. These defaults used
# to describe the public eWINE set at depth 4, which had not been what shipped since
# c70cdc1e, and a bare run silently replaced the shipped tree with a different model
# trained on different data -- while every gate passed, because the gates certify the
# generated C against whatever was just fitted, not against what ships. The README's
# one-line regenerate command is what makes this load-bearing.
FEATURES = os.environ.get("FEATURES", "ai/tinyml/captures-door-2026-08-07-ranged.csv")

# SUBSET must match bakeoff.py's. Only two of the twelve feature sets are reachable on
# this hardware at all: the other ten need the 64-tap CIR window, and an image that reads
# the accumulator cannot range at all on the DWM3001CDK (RESULTS.md Result 7). The capture
# that works emits ipatovPower, ipatovF1..F3 and ipatovAccumCount, so a 14-feature seam
# would give firmware an API it can never call.
SUBSET = os.environ.get("SUBSET", "resid")
SCALAR_FEATURES = ["fp_pwr", "rx_pwr", "pwr_diff", "rxpacc"]
# `resid` is the shipped set. Replacing fp_pwr with its free-space-corrected residual and
# dropping the two features that were carrying nothing costs two features and gains 13.5
# points on the strictest split available (RESULTS.md Result 11). It needs the ranging
# distance, which `scalar` does not; keep `scalar` reachable for a capture taken without
# --distance, which is what the pre-Result-11 sessions were.
RESID_FEATURES = ["fp_resid", "rx_pwr"]
SUBSETS = {"scalar": SCALAR_FEATURES, "resid": RESID_FEATURES}

SEED = int(os.environ.get("SEED", "42"))
# 2, not 4: on 544 captures rather than 42,000, depth 4 overfits. Depth 2 holds 0.7729
# with a whole capture session held out against depth 4's lower number, and it is the
# held-out-session figure that predicts an install this board has never seen.
DEPTH = int(os.environ.get("DEPTH", "2"))
N_VECTORS = int(os.environ.get("VECTORS", "256"))

# Eight entries plus an end sentinel. Sized by measurement, not taste: on all 544
# captured receptions, rounding the residual to steps as coarse as 2 dB moves balanced
# accuracy by +0.0005, and table sizes from 8 to 64 entries all bottom out at the same
# 0.183 dB maximum error because the dominant term is range arriving as a whole number of
# centimetres, not the interpolation.
LOG2_TABLE_BITS = 3
LOG2_TABLE_H = "modules/woz_ml/src/woz_ml_log2_table.h"

# eWINE's PRF-64 constant and the DW3000's channel-area scaling, both mirrored from
# parse_alab.py. Gate 4 recomputes the captured powers through them, so a change here
# that is not also made there turns the gate into a comparison of nothing.
A_CONST_PRF64 = 121.74
CHANNEL_AREA_SHIFT = 17
# Mirrored in woz_ml_range.c and in parse_alab.py. All three have to agree or the model
# is trained through one definition and run through another.
MIN_RANGE_CM, MAX_RANGE_CM = 10, 2000
K_DB_PER_LOG2 = 20.0 / np.log2(10.0)
LOG2_REF_CM = np.log2(100.0)

TREE_H = "modules/woz_ml/src/woz_ml_los_tree.h"
SCALER_H = "modules/woz_ml/src/woz_ml_los_scaler.h"
FEATURES_H = "modules/woz_ml/include/woz_ml_los_features.h"
VECTORS_H = "tests/host/data/woz_ml_los_vectors.h"

# Must match bakeoff.py exactly: the tree is trained on the quantised features,
# so this mapping is part of the model, not a preprocessing detail.
Q_SPAN = 32000.0
Q_OFFSET = 16000.0

BANNER = ("/* GENERATED by ai/tinyml/gen_model.py -- do not hand-edit.\n"
          " * Regenerate with:\n"
          " *     ai/tinyml/.venv/bin/python ai/tinyml/gen_model.py\n"
          " */\n")


@contextlib.contextmanager
def emlearn_scratch():
    """Run emlearn's compile-and-load from a throwaway directory.

    `predict()` builds a Python-callable extension at a FIXED path: `name =
    'mytree'` (emlearn/trees.py) under `temp_dir='tmp'` (emlearn/common.py),
    i.e. `./tmp/mytree.{c,h,o}`, regardless of the name passed to `save()`.
    Called from the repository root that drops build spill into the tree, which
    the mal-diff gate blocks and is right to. It is also why two emlearn
    processes must never share a working directory: they overwrite each other's
    extension and one silently loads the other's, which looks exactly like a
    model bug and is not one.
    """
    cwd = os.getcwd()
    with tempfile.TemporaryDirectory(prefix="emlearn-") as d:
        os.chdir(d)
        try:
            yield
        finally:
            os.chdir(cwd)


def log2_c(v, bits=LOG2_TABLE_BITS):
    """Exactly what woz_ml_log2.c computes, in float32 throughout.

    Mirrors the C step for step -- leading-bit exponent, table lookup, linear
    interpolation -- because gates 3 and 4 below are only meaningful if this is the
    same arithmetic and not merely the same idea. Zero maps to 0.0 as it does there.

    Takes an integer array of any width: numpy handles the exponent in float64,
    which is exact for the magnitudes involved (the largest is a 17-bit channel
    area shifted left by 17, so under 2^34 and well inside a double's 53 bits).
    """
    v = np.asarray(v, dtype=np.float64)
    n = 1 << bits
    tab = np.log2(1.0 + np.arange(n + 1) / n).astype(np.float32)
    out = np.zeros(v.shape, dtype=np.float32)
    ok = v > 0
    if not np.any(ok):
        return out
    vv = v[ok]
    exponent = np.floor(np.log2(vv)).astype(np.int64)
    mantissa = vv / np.exp2(exponent.astype(np.float64)) - 1.0
    idx = np.clip((mantissa * n).astype(np.int64), 0, n - 1)
    w = np.float32(mantissa * n - idx)
    out[ok] = (np.float32(exponent) + tab[idx] + w * (tab[idx + 1] - tab[idx])).astype(
        np.float32)
    return out


def range_correction_c(dist_cm):
    """Exactly what woz_ml_range.c computes: the clamp, then a change of base."""
    d = np.clip(np.asarray(dist_cm, dtype=np.int64), MIN_RANGE_CM, MAX_RANGE_CM)
    return (np.float32(K_DB_PER_LOG2)
            * (log2_c(d) - np.float32(LOG2_REF_CM))).astype(np.float32)


def pwr_db_c(numerator, count):
    """Exactly what woz_ml_feat.c's pwr_db() computes, in float32 throughout."""
    k = np.float32(10.0 / np.log2(10.0))
    return (k * (log2_c(numerator) - np.float32(2.0) * log2_c(count))
            - np.float32(A_CONST_PRF64)).astype(np.float32)


def recover_cia(fp_pwr, rx_pwr, acc):
    """Invert parse_alab.py's power formulas back to the integers they were read from.

    The tracked CSV carries derived dB values, not registers, because the raw capture
    logs are not in the repo. But the forward map is invertible and the CSV holds 12
    significant figures, so the integers come back exactly:

        num  = C^2 * 10^((fp_pwr + A)/10)
        area = C^2 * 10^((rx_pwr + A)/10) / 2^17

    Returned rounded to integers, which is only legitimate if the round trip is
    lossless -- gate 4 checks that before it uses them, and refuses rather than
    quietly testing the C against a number the hardware never produced.
    """
    acc2 = np.asarray(acc, dtype=np.float64) ** 2
    num = acc2 * np.power(10.0, (np.asarray(fp_pwr, dtype=np.float64) + A_CONST_PRF64) / 10.0)
    area = (acc2 * np.power(10.0, (np.asarray(rx_pwr, dtype=np.float64) + A_CONST_PRF64) / 10.0)
            / float(1 << CHANNEL_AREA_SHIFT))
    return np.round(num), np.round(area)


def write_log2_table(path, bits=LOG2_TABLE_BITS):
    n = 1 << bits
    tab = np.log2(1.0 + np.arange(n + 1) / n).astype(np.float32)
    # "%.9g" of 0.0 is "0", and "0f" is not a C float literal. Force a decimal point.
    def lit(v):
        s = f"{v:.9g}"
        return s + ("f" if ("." in s or "e" in s) else ".0f")

    body = "\n".join(f"\t{lit(v)}," for v in tab)
    with open(path, "w") as f:
        f.write(BANNER + f"""
#ifndef WOZ_ML_LOG2_TABLE_H
#define WOZ_ML_LOG2_TABLE_H

/* log2(1 + i/{n}) for i in 0..{n}. The last entry is the interpolation sentinel, so a
 * mantissa in the top bucket has something to interpolate towards. See
 * woz_ml_log2.c for why {n} entries is enough. */
#define WOZ_ML_LOG2_TABLE_BITS {bits}

static const float woz_ml_log2_tab[{n + 1}] = {{
{body}
}};

#endif /* WOZ_ML_LOG2_TABLE_H */
""")


def quantise_f64(X, lo, scale):
    """The bench path: float64 throughout, which is what sklearn trained on."""
    q = (X - lo) * scale - Q_OFFSET
    return np.clip(np.round(q), -32768, 32767).astype(np.int16)


def quantise_f32(X, lo, scale):
    """The firmware path, simulated exactly.

    Every operand and intermediate is float32, and the rounding is the
    add-half-and-truncate that woz_ml_los.c does -- half away from zero -- not
    numpy's default half-to-even. That difference is the entire point: this has
    to model what the target computes, not what is convenient here, or the gate
    it feeds proves nothing about the target."""
    x = X.astype(np.float32)
    q = (x - lo.astype(np.float32)) * scale.astype(np.float32)
    q = (q - np.float32(Q_OFFSET)).astype(np.float32)
    r = np.where(q >= np.float32(0.0),
                 np.floor(q + np.float32(0.5)),
                 np.ceil(q - np.float32(0.5)))
    return np.clip(r, -32768, 32767).astype(np.int16)


def c_float(v):
    """A float32 literal that round-trips.

    %.9g is enough digits to recover any float32 exactly, but it drops the
    decimal point on integral values, and `314f` is not a C literal at all --
    the compiler reads it as an invalid digit in a decimal constant. Anything
    without a point or an exponent gets one."""
    f = np.float32(v)
    if not np.isfinite(f):
        raise SystemExit(f"non-finite constant {v!r}: the feature set is broken")
    s = f"{f:.9g}"
    if not any(c in s for c in ".eE"):
        s += ".0"
    return s + "f"


def leaf_ids(model, Q):
    """Which leaf each sample lands in. The stratification key."""
    return model.apply(Q)


def prune_agreeing_subtrees(model):
    """Collapse every subtree whose leaves all predict the same class.

    A tree fitted for accuracy keeps splits that separate samples without
    changing the answer -- both children predict the same class -- because they
    still lower impurity. In C those come out as `if (x < t) { return 1; } else
    { return 1; }`. The compiler folds them, so this buys no flash; what it buys
    is a generated file that says what the model actually decides, and a clean
    bill from clang-tidy's bugprone-branch-clone, which is right to flag them.

    The tree has to be REBUILT, not just rewired: emlearn's flatten_tree()
    asserts that the nodes it walks equal tree_.node_count, so orphaned nodes
    left behind in the arrays fail with a bare `AssertionError: (13, 31)`.

    Semantics-preserving by construction, and checked anyway: the caller refuses
    to continue unless the pruned tree predicts identically to the unpruned one
    on all 42,000 samples, not just the held-out ones.

    Returns (nodes before, nodes after).
    """
    from sklearn.tree._tree import Tree

    t = model.tree_
    state = t.__getstate__()
    nodes, values = state["nodes"], state["values"]
    left, right = nodes["left_child"], nodes["right_child"]

    # Pass 1: which nodes become leaves. Bottom-up, so a collapsed child lets its
    # parent collapse too.
    collapse = set()

    def unified_class(n):
        """The class this subtree predicts, or None if its leaves disagree."""
        if left[n] == -1:
            return int(np.argmax(values[n][0]))
        a, b = unified_class(left[n]), unified_class(right[n])
        if a is not None and a == b:
            # emlearn reads a leaf's class from `values`, not from this return,
            # so the node's own distribution has to agree with its leaves. For
            # two classes it must: if every leaf has count[a] > count[other], so
            # does their sum. Asserted rather than argued, because the day this
            # goes multiclass the argument stops holding and nothing else would
            # notice.
            assert int(np.argmax(values[n][0])) == a, (
                f"node {n}: leaves say class {a} but the node's own "
                f"distribution {values[n][0]} says otherwise")
            collapse.add(n)
            return a
        return None

    unified_class(0)

    # Pass 2: renumber the reachable nodes into a fresh, dense tree.
    order, remap = [], {}

    def walk(n, depth):
        remap[n] = len(order)
        order.append(n)
        if n in collapse or left[n] == -1:
            return depth
        return max(walk(left[n], depth + 1), walk(right[n], depth + 1))

    max_depth = walk(0, 0)

    new_nodes = np.empty(len(order), dtype=nodes.dtype)
    new_values = np.empty((len(order),) + values.shape[1:], dtype=values.dtype)
    for new_i, old_i in enumerate(order):
        new_nodes[new_i] = nodes[old_i]
        new_values[new_i] = values[old_i]
        if old_i in collapse or left[old_i] == -1:
            new_nodes[new_i]["left_child"] = -1
            new_nodes[new_i]["right_child"] = -1
            new_nodes[new_i]["feature"] = -2       # sklearn's TREE_UNDEFINED
            new_nodes[new_i]["threshold"] = -2.0
        else:
            new_nodes[new_i]["left_child"] = remap[left[old_i]]
            new_nodes[new_i]["right_child"] = remap[right[old_i]]

    pruned = Tree(t.n_features, np.array([t.n_classes[0]], dtype=np.intp),
                  t.n_outputs)
    pruned.__setstate__({"max_depth": max_depth, "node_count": len(order),
                         "nodes": new_nodes, "values": new_values})
    before = t.node_count
    model.tree_ = pruned
    return before, len(order)


def emit_features_h(names, path):
    """The public half: how many features, and in what order."""
    lines = [BANNER, "", "#ifndef WOZ_ML_LOS_FEATURES_H", "#define WOZ_ML_LOS_FEATURES_H",
             "", f"#define WOZ_ML_LOS_N_FEATURES {len(names)}", "",
             "/* Feature order is part of the model. A caller that fills the array in a",
             " * different order gets a confident wrong answer, not an error, because every",
             " * float is a legal value for every feature. Definitions of what each one",
             " * means are in ai/tinyml/extract_features.py, which is the only authority. */",
             "enum woz_ml_los_feature {"]
    for i, nm in enumerate(names):
        lines.append(f"\tWOZ_ML_LOS_F_{nm.upper()} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* WOZ_ML_LOS_FEATURES_H */")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def emit_scaler_h(names, lo, scale, path):
    """The private half: `static const` arrays, so exactly one TU may include it."""
    lines = [BANNER, "", "#ifndef WOZ_ML_LOS_SCALER_H", "#define WOZ_ML_LOS_SCALER_H", "",
             "#include \"woz_ml_los_features.h\"", "",
             "/* Affine map into the int16 space the tree was trained in:",
             " *     q = (x - lo) * scale - 16000, rounded half away from zero, clamped.",
             " * float32 on purpose: gen_model.py proves this agrees with the float64 path",
             " * sklearn trained through, on every held-out sample.",
             " *",
             " * static const, so include this from ONE translation unit. It is in src/ and",
             " * not in include/ for exactly that reason. */",
             "static const float woz_ml_los_lo[WOZ_ML_LOS_N_FEATURES] = {"]
    for nm, v in zip(names, lo):
        lines.append(f"\t{c_float(v)},\t/* {nm} */")
    lines.append("};")
    lines.append("")
    lines.append("static const float woz_ml_los_scale[WOZ_ML_LOS_N_FEATURES] = {")
    for nm, v in zip(names, scale):
        lines.append(f"\t{c_float(v)},\t/* {nm} */")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* WOZ_ML_LOS_SCALER_H */")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def emit_vectors_h(X, expect, names, path, full_n, c_mismatch, f32_mismatch, acc):
    lines = [BANNER, "",
             "#ifndef WOZ_ML_LOS_VECTORS_H", "#define WOZ_ML_LOS_VECTORS_H", "",
             "#include <stdint.h>", "",
             f"/* {len(X)} vectors, stratified over the {DEPTH}-deep tree's leaves so every",
             " * decision path is exercised. `expect` is sklearn's own answer, which is what",
             " * makes these a certification rather than a regression pin: the C is being",
             " * held to the training-side model, not to its own past behaviour.",
             " *",
             f" * On the full {full_n}-sample held-out set at generation time:",
             f" *   generated C vs sklearn        {c_mismatch} mismatches",
             f" *   float32 scaler vs float64     {f32_mismatch} mismatches",
             f" *   test accuracy                 {acc:.4f}",
             " *",
             " * Features are raw physical units, so these exercise the scaler too. */", ""]
    lines.append(f"#define WOZ_ML_LOS_VEC_COUNT {len(X)}")
    lines.append(f"#define WOZ_ML_LOS_VEC_FEATURES {X.shape[1]}")
    lines.append("")
    lines.append("/* " + ", ".join(names) + " */")
    lines.append("static const float woz_ml_los_vec_in"
                 "[WOZ_ML_LOS_VEC_COUNT][WOZ_ML_LOS_VEC_FEATURES] = {")
    for row in X:
        lines.append("\t{" + ", ".join(c_float(v) for v in row) + "},")
    lines.append("};")
    lines.append("")
    lines.append("static const int8_t woz_ml_los_vec_expect[WOZ_ML_LOS_VEC_COUNT] = {")
    for i in range(0, len(expect), 32):
        lines.append("\t" + ", ".join(str(int(v)) for v in expect[i:i + 32]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* WOZ_ML_LOS_VECTORS_H */")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    X, y, names, _ = read_features(FEATURES)
    # Kept before the subset drops them: gate 3 has to recompute the residual the way the
    # target will, from the raw power and the raw range, not from the column derived here,
    # and gate 4 needs rx_pwr and rxpacc as well to recover the registers underneath.
    raw = {n: X[:, names.index(n)]
           for n in ("fp_pwr", "rx_pwr", "rxpacc", "dist_cm") if n in names}
    if SUBSET != "all":
        if SUBSET not in SUBSETS:
            raise SystemExit(f"unknown SUBSET={SUBSET}; pick all/{'/'.join(SUBSETS)}")
        wanted = SUBSETS[SUBSET]
        missing = [n for n in wanted if n not in names]
        if missing:
            raise SystemExit(f"SUBSET={SUBSET}: {missing} absent from {FEATURES}")
        keep = [names.index(n) for n in wanted]  # generator order, not file order
        X, names = X[:, keep], wanted
    print(f"{X.shape[0]} samples, {X.shape[1]} features: {', '.join(names)}")

    Xtr, Xtmp, ytr, ytmp = train_test_split(X, y, test_size=0.30,
                                            random_state=SEED, stratify=y)
    _, Xte, _, yte = train_test_split(Xtmp, ytmp, test_size=0.50,
                                      random_state=SEED, stratify=ytmp)

    lo = Xtr.min(axis=0)
    span = np.maximum(Xtr.max(axis=0) - lo, 1e-9)
    scale = Q_SPAN / span

    Qtr = quantise_f64(Xtr, lo, scale)
    Qte = quantise_f64(Xte, lo, scale)

    model = DecisionTreeClassifier(max_depth=DEPTH, random_state=SEED).fit(Qtr, ytr)
    acc = float(model.score(Qte, yte))
    print(f"\ndtree-d{DEPTH}: test accuracy {acc:.4f}, "
          f"{model.get_n_leaves()} leaves, {model.tree_.node_count} nodes")

    # --- gate 0: pruning agreeing subtrees must change nothing --------------
    Qall = quantise_f64(X, lo, scale)
    before = model.predict(Qall)
    n_before, n_after = prune_agreeing_subtrees(model)
    after = model.predict(Qall)
    prune_mismatch = int((before != after).sum())
    print(f"gate 0  pruned {n_before} nodes to {n_after}: "
          f"{prune_mismatch} / {len(y)} predictions changed")
    if prune_mismatch:
        raise SystemExit("\nREFUSING TO WRITE. Pruning changed a prediction, which it "
                         "must never do; the collapse rule is wrong.")

    sk = model.predict(Qte)

    # --- gate 1: the generated C must reproduce sklearn ---------------------
    cm = emlearn.convert(model, method="inline", dtype="int16_t")
    with emlearn_scratch():
        c_pred = cm.predict(Qte)
    c_mismatch = int((c_pred != sk).sum())
    print(f"gate 1  generated C vs sklearn:      {c_mismatch} / {len(yte)} mismatches")

    # --- gate 2: the float32 scaler must agree with the float64 one ---------
    Q32 = quantise_f32(Xte, lo, scale)
    f32_pred = model.predict(Q32)
    f32_mismatch = int((f32_pred != sk).sum())
    q_diff = int((Q32 != Qte).sum())
    print(f"gate 2  float32 scaler vs float64:   {f32_mismatch} / {len(yte)} mismatches "
          f"({q_diff} of {Qte.size} quantised values differ by rounding)")

    # --- gate 3: the libm-free range correction must change no class --------
    # Only when the model actually uses fp_resid. The feature was trained through
    # numpy's log10 in float64; the target computes it from a leading-bit exponent and
    # an 8-entry table. That is a different function, so it has to be shown to be the
    # same classifier, which is the same standard gate 2 holds the scaler to.
    resid_mismatch = 0
    uses_resid = "fp_resid" in names
    if uses_resid:
        if not {"fp_pwr", "dist_cm"} <= raw.keys():
            raise SystemExit("SUBSET uses fp_resid but the feature file carries no "
                             "fp_pwr/dist_cm to recompute it from; re-run "
                             "parse_alab.py with --distance.")
        approx = X.copy()
        approx[:, names.index("fp_resid")] = (
            raw["fp_pwr"] + range_correction_c(np.round(raw["dist_cm"])))
        exact_pred = model.predict(quantise_f64(X, lo, scale))
        approx_pred = model.predict(quantise_f64(approx, lo, scale))
        resid_mismatch = int((exact_pred != approx_pred).sum())
        err = np.abs(approx[:, names.index("fp_resid")] - X[:, names.index("fp_resid")])
        print(f"gate 3  table vs log10 residual:     {resid_mismatch} / {len(y)} "
              f"mismatches (max {err.max():.3f} dB error)")

    # --- gate 4: the on-target feature extractor must change no class -------
    # Gates 1-3 all start from features that this generator computed in float64.
    # Nothing checked that the TARGET, starting from the registers the radio
    # actually reports, arrives at the same features -- and that path is the one
    # with the 64-bit sums, the 2^17 channel-area scaling and three interpolated
    # logarithms in it. woz_ml_feat.c is that path; this is its gate.
    #
    # The raw capture logs are not tracked (data/ is gitignored and ~1.8 GB), so
    # the registers are recovered by inverting the formulas that produced the
    # tracked CSV. That is only legitimate if the inversion is lossless, which is
    # checked first and separately: if the recovered integers do not reproduce the
    # recorded dB to well under a millidecibel, they are not the integers the
    # radio reported and this gate is measuring nothing.
    feat_mismatch = 0
    feat_checked = 0
    if uses_resid and {"rx_pwr", "rxpacc"} <= raw.keys():
        # NOT `acc`: that name holds the model's test accuracy in this function, and
        # shadowing it here silently poisons the vectors header written later.
        count = np.round(raw["rxpacc"]).astype(np.int64)
        num_i, area_i = recover_cia(raw["fp_pwr"], raw["rx_pwr"], count)
        good = (count > 0) & (num_i > 0) & (area_i > 0)

        # Round trip, in float64, against the dB the CSV recorded.
        c2 = count[good].astype(np.float64) ** 2
        rt_fp = 10.0 * np.log10(num_i[good] / c2) - A_CONST_PRF64
        rt_rx = (10.0 * np.log10(area_i[good] * float(1 << CHANNEL_AREA_SHIFT) / c2)
                 - A_CONST_PRF64)
        rt_err = max(float(np.abs(rt_fp - raw["fp_pwr"][good]).max()),
                     float(np.abs(rt_rx - raw["rx_pwr"][good]).max()))
        if rt_err > 1e-6:
            raise SystemExit(
                f"\nREFUSING TO WRITE. Recovering the CIA registers from the CSV is "
                f"lossy: {rt_err:.3g} dB.\nGate 4 would be comparing the C against "
                "numbers the radio never produced. Either the CSV lost precision or\n"
                "parse_alab.py's power formulas no longer match the ones inverted here.")

        # Now the target's arithmetic, float32 throughout, from those integers.
        fp_c = pwr_db_c(num_i[good], count[good])
        rx_c = pwr_db_c(area_i[good] * float(1 << CHANNEL_AREA_SHIFT), count[good])
        approx = X[good].copy()
        approx[:, names.index("fp_resid")] = fp_c + range_correction_c(
            np.round(raw["dist_cm"][good]))
        approx[:, names.index("rx_pwr")] = rx_c

        exact_pred = model.predict(quantise_f64(X[good], lo, scale))
        approx_pred = model.predict(quantise_f64(approx, lo, scale))
        feat_mismatch = int((exact_pred != approx_pred).sum())
        feat_checked = int(good.sum())
        fp_err = float(np.abs(fp_c - raw["fp_pwr"][good]).max())
        rx_err = float(np.abs(rx_c - raw["rx_pwr"][good]).max())
        print(f"gate 4  woz_ml_feat.c vs float64:    {feat_mismatch} / {feat_checked} "
              f"mismatches (max {max(fp_err, rx_err):.4f} dB on the powers, "
              f"registers recovered to {rt_err:.1e} dB)")

    if c_mismatch or f32_mismatch or resid_mismatch or feat_mismatch:
        raise SystemExit(
            f"\nREFUSING TO WRITE. gate 1 = {c_mismatch}, gate 2 = {f32_mismatch}, "
            f"gate 3 = {resid_mismatch}, gate 4 = {feat_mismatch}.\n"
            "All must be zero: the point of shipping a single tree rather than a\n"
            "forest is that its on-target behaviour can be certified against the\n"
            "model that was trained. A non-zero count means it cannot.")

    # --- golden vectors, stratified over leaves -----------------------------
    leaves = leaf_ids(model, Qte)
    uniq = np.unique(leaves)
    rng = np.random.default_rng(SEED)
    per_leaf = max(1, N_VECTORS // len(uniq))
    picks = []
    for leaf in uniq:
        idx = np.flatnonzero(leaves == leaf)
        picks.append(rng.choice(idx, size=min(per_leaf, len(idx)), replace=False))
    sel = np.sort(np.concatenate(picks))
    print(f"\ngolden vectors: {len(sel)} over {len(uniq)} leaves "
          f"({per_leaf} per leaf, capped by leaf population)")

    for p in (TREE_H, FEATURES_H, VECTORS_H):
        os.makedirs(os.path.dirname(p), exist_ok=True)

    cm.save(name="woz_ml_los_tree", file=TREE_H)
    # emlearn writes its own one-line banner; prepend ours so the file says where
    # it came from and how to rebuild it.
    with open(TREE_H) as f:
        body = f.read()
    with open(TREE_H, "w") as f:
        f.write(BANNER + body)

    emit_features_h(names, FEATURES_H)
    emit_scaler_h(names, lo, scale, SCALER_H)
    # Unconditional, unlike the model headers: woz_ml_feat.c takes the logarithm of
    # the raw registers whatever the feature set is, so the table is not optional
    # even for a SUBSET that drops fp_resid.
    write_log2_table(LOG2_TABLE_H)
    emit_vectors_h(Xte[sel], sk[sel], names, VECTORS_H,
                   len(yte), c_mismatch, f32_mismatch, acc)

    nodes = len(re.findall(r"\bif \(features\[", open(TREE_H).read()))

    # verify.sh's `format` gate clang-formats every tracked modules/*.{c,h}, and
    # it does not care that three of these are generated. Formatting them here is
    # what keeps a regeneration from breaking the build for whoever runs it.
    # tests/host/ is not format-gated, so the vectors are left alone.
    if shutil.which("clang-format"):
        fmt = [TREE_H, FEATURES_H, SCALER_H, LOG2_TABLE_H]
        subprocess.run(["clang-format", "-i", *fmt], check=True)
    else:
        print("\nWARNING: clang-format not found, so the generated headers are\n"
              "         unformatted and verify.sh's `format` gate will fail.\n"
              "         Fix: install clang-format and re-run.")

    print(f"\nwrote {TREE_H} ({nodes} comparisons)")
    print(f"wrote {FEATURES_H}")
    print(f"wrote {SCALER_H}")
    print(f"wrote {VECTORS_H}")


if __name__ == "__main__":
    main()
