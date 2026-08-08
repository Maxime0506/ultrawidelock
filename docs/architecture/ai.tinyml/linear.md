<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/linear.py`

Does a linear boundary beat the shipped two-split tree, and can its margin be
the graded confidence the tree cannot give?

Run from the repository root:
    ai/tinyml/.venv/bin/python ai/tinyml/linear.py

WHY ASK. `modules/woz_ml/src/woz_ml_los_tree.h` ships a depth-2 tree: two
comparisons, four leaves. Its generated `woz_ml_los_tree_predict_proba()` writes
1.0f into one slot and 0.0f into the other, because a single tree's leaf IS the
class. So the module can answer "obstructed" and cannot answer "how sure", and
RESULTS.md `Next` item 6 -- wire the classifier to a decision, which has to know
what a wrong answer costs at a door -- has no graded quantity to read. A linear
boundary has one for free: the signed distance from it.

WHAT WOULD MAKE IT SHIPPABLE, all three or none:

1. It has to beat the tree on the same splits. Both are fitted on the same two
   features, so this is a fair fight between decision-boundary shapes: two
   axis-aligned steps against one oblique line.
2. The margin has to be MONOTONE in correctness. A confidence that does not
   predict correctness is a number, not a confidence, and shipping it would be
   worse than shipping nothing.
3. It has to stay libc-free, which `arm-none-eabi-nm -u` checks and `log10f`
   would end. A class decision needs only `w0*x0 + w1*x1 + b > 0`, two multiplies
   on a Cortex-M4F's FPU. The logistic PROBABILITY would need exp(), so it is
   never computed on target: the margin is monotone in the probability, so every
   threshold on one is a threshold on the other, and the exp is a host-side
   convenience that no firmware has to reproduce.

THE SPLIT IS THE BLOCKED CV OF variance.py, not a session hold-out. The tracked
CSV carries no session column, so the honest 0.7729 whole-capture number in
Result 11 cannot be reproduced from this artifact by any script here. Contiguous
blocks stand in. Every model below meets the same folds, so the COMPARISON is
sound even where the absolute level is optimistic.

Self-contained on purpose: variance.py is being edited concurrently for a
different experiment, and importing it would couple this result to that file's
state mid-run. The twenty lines of loader and grouping are duplicated for that
reason and no other.

**discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

## API

### `blocked_groups(label)`
`ai/tinyml/linear.py:93`

Contiguous blocks within each label run, as GroupKFold groups.

**called by** `main`

### `out_of_fold(model_fn, X, y, groups)`
`ai/tinyml/linear.py:113`

Out-of-fold predictions, and the margin where the model has one.

Every number in this file is out-of-fold: a margin scored on rows the model
was fitted on would be confident for the wrong reason.

**called by** `hybrid`, `main`

### `emit_module(w, b, X, margin)`
`ai/tinyml/linear.py:208`

Write the coefficients into modules/woz_ml/ and the vectors into the suite.

Generated rather than copied by hand for the reason gen_model.py gives: a
constant transcribed once is a constant that drifts silently the next time
the model is refitted, and nothing downstream can tell.

THE GATE. float32 is what the target has and float64 is what this fit is in,
so the header is refused unless every one of the 544 receptions lands on the
same side of the boundary in both. A confidence that is a hair different on
target is fine; a class that flips is the model changing when it crosses the
seam.

**called by** `main`

### `codesize()`
`ai/tinyml/linear.py:290`

Cross-compile the SHIPPED source for the CDK's core and measure it.

modules/woz_ml/src/woz_ml_lin.c itself, not a copy of it, so the figure
cannot drift from what a firmware build would link. Same flags, compiler and
reading as codesize.py, so the number sits beside the tree's without an
asterisk. Nothing is linked: -ffunction-sections puts each function in its
own section and --gc-sections can only remove at link time, never add, so
the per-section object sizes are an upper bound on what a link keeps.

**called by** `main`

### `hybrid(X, y, groups)`
`ai/tinyml/linear.py:328`

If the line loses on accuracy, can it still grade the TREE's answers?

The margin being monotone in the LINEAR model's own correctness is not the
question a hybrid asks. The tree would still decide; the line would only say
how sure to be. So the test is whether |margin| predicts the correctness of
the TREE's prediction, on rows neither model has seen.

Beside it, the confidence that costs nothing extra: a depth-2 tree has four
leaves, and each leaf's training-set purity is a per-leaf probability --
which is exactly what emlearn emits at `leaf_bits=8`, one byte per leaf per
class. If purity grades as well as the margin does, the two multiplies buy
nothing and the cheaper answer wins.

**called by** `main`  ·  **calls** `linear_model`, `out_of_fold`

<details><summary>Undocumented (3)</summary>

- `load`
- `linear_model`
- `main`

</details>
