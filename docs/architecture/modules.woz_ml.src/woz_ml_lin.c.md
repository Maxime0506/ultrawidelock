<!-- generated documentation — edit the source, not this file -->
# `modules/woz_ml/src/woz_ml_lin.c`

@file woz_ml_lin.c — how sure the tree is, which the tree itself cannot say.
A single decision tree's leaf IS its class, so woz_ml_los_classify() answers
"obstructed" and has no second quantity to offer. This supplies one: the
distance from a linear boundary fitted on the same two features, which was
measured to separate the receptions the tree gets right from the ones it gets
wrong. See woz_ml.h for the measurement and ai/tinyml/RESULTS.md Result 16.
The coefficients are generated; only the arithmetic is written by hand, and
there is not much of it.

**depends on** [`modules/woz_ml/include/woz_ml.h`](../modules.woz_ml.include/woz_ml.h.md), [`modules/woz_ml/src/woz_ml_los_lin.h`](woz_ml_los_lin.h.md)  ·  **discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

<details><summary>Undocumented (1)</summary>

- `woz_ml_los_confidence` — tested: woz ml

</details>
