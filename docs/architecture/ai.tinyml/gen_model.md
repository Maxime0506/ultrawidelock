<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/gen_model.py`

Generate the shipped LOS/NLOS tree, its scaler, and the golden vectors.

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

**depends on** [`ai/tinyml/features_io.py`](features_io.md)  ·  **used by** [`ai/tinyml/leaf_bits.py`](leaf_bits.md)  ·  **discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md), [`modules/README.md`](../../../modules/README.md), [`modules/woz_ml/README.md`](../../../modules/woz_ml/README.md)

## API

### `emlearn_scratch()`
`ai/tinyml/gen_model.py:126`

Run emlearn's compile-and-load from a throwaway directory.

`predict()` builds a Python-callable extension at a FIXED path: `name =
'mytree'` (emlearn/trees.py) under `temp_dir='tmp'` (emlearn/common.py),
i.e. `./tmp/mytree.{c,h,o}`, regardless of the name passed to `save()`.
Called from the repository root that drops build spill into the tree, which
the mal-diff gate blocks and is right to. It is also why two emlearn
processes must never share a working directory: they overwrite each other's
extension and one silently loads the other's, which looks exactly like a
model bug and is not one.

**called by** `main`, `run`

### `log2_c(v, bits=LOG2_TABLE_BITS)`
`ai/tinyml/gen_model.py:147`

Exactly what woz_ml_log2.c computes, in float32 throughout.

Mirrors the C step for step -- leading-bit exponent, table lookup, linear
interpolation -- because gates 3 and 4 below are only meaningful if this is the
same arithmetic and not merely the same idea. Zero maps to 0.0 as it does there.

Takes an integer array of any width: numpy handles the exponent in float64,
which is exact for the magnitudes involved (the largest is a 17-bit channel
area shifted left by 17, so under 2^34 and well inside a double's 53 bits).

**called by** `pwr_db_c`, `range_correction_c`

### `range_correction_c(dist_cm)`
`ai/tinyml/gen_model.py:175`

Exactly what woz_ml_range.c computes: the clamp, then a change of base.

**called by** `main`  ·  **calls** `log2_c`

### `pwr_db_c(numerator, count)`
`ai/tinyml/gen_model.py:182`

Exactly what woz_ml_feat.c's pwr_db() computes, in float32 throughout.

**called by** `main`  ·  **calls** `log2_c`

### `recover_cia(fp_pwr, rx_pwr, acc)`
`ai/tinyml/gen_model.py:189`

Invert parse_alab.py's power formulas back to the integers they were read from.

The tracked CSV carries derived dB values, not registers, because the raw capture
logs are not in the repo. But the forward map is invertible and the CSV holds 12
significant figures, so the integers come back exactly:

    num  = C^2 * 10^((fp_pwr + A)/10)
    area = C^2 * 10^((rx_pwr + A)/10) / 2^17

Returned rounded to integers, which is only legitimate if the round trip is
lossless -- gate 4 checks that before it uses them, and refuses rather than
quietly testing the C against a number the hardware never produced.

**called by** `main`

### `quantise_f64(X, lo, scale)`
`ai/tinyml/gen_model.py:237`

The bench path: float64 throughout, which is what sklearn trained on.

**called by** `main`, `run`

### `quantise_f32(X, lo, scale)`
`ai/tinyml/gen_model.py:243`

The firmware path, simulated exactly.

Every operand and intermediate is float32, and the rounding is the
add-half-and-truncate that woz_ml_los.c does -- half away from zero -- not
numpy's default half-to-even. That difference is the entire point: this has
to model what the target computes, not what is convenient here, or the gate
it feeds proves nothing about the target.

**called by** `main`

### `c_float(v)`
`ai/tinyml/gen_model.py:260`

A float32 literal that round-trips.

%.9g is enough digits to recover any float32 exactly, but it drops the
decimal point on integral values, and `314f` is not a C literal at all --
the compiler reads it as an invalid digit in a decimal constant. Anything
without a point or an exponent gets one.

**called by** `emit_scaler_h`, `emit_vectors_h`

### `leaf_ids(model, Q)`
`ai/tinyml/gen_model.py:276`

Which leaf each sample lands in. The stratification key.

**called by** `main`

### `prune_agreeing_subtrees(model)`
`ai/tinyml/gen_model.py:281`

Collapse every subtree whose leaves all predict the same class.

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

**called by** `main`  ·  **calls** `unified_class`, `walk`

### `unified_class(n)`
`ai/tinyml/gen_model.py:312`

The class this subtree predicts, or None if its leaves disagree.

**called by** `prune_agreeing_subtrees`

### `emit_features_h(names, path)`
`ai/tinyml/gen_model.py:368`

The public half: how many features, and in what order.

**called by** `main`

### `emit_scaler_h(names, lo, scale, path)`
`ai/tinyml/gen_model.py:386`

The private half: `static const` arrays, so exactly one TU may include it.

**called by** `main`  ·  **calls** `c_float`

<details><summary>Undocumented (5)</summary>

- `write_log2_table`
- `lit`
- `walk`
- `emit_vectors_h`
- `main`

</details>
