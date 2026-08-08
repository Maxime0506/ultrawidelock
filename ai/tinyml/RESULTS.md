# TinyML bake-off results: LOS/NLOS, from the eWINE data set to this board

Runs 2026-08-06 (Results 1-7, host only, public data) and 2026-08-07 (Results 8-9, this
board and an iPhone). Reproduce the host half with:

    ai/tinyml/.venv/bin/python ai/tinyml/extract_features.py
    ai/tinyml/.venv/bin/python ai/tinyml/bakeoff.py
    PORTABLE=1 WIN_PRE=32 WIN_POST=32 OUT=ai/tinyml/features_dw3000.npz \
        ai/tinyml/.venv/bin/python ai/tinyml/extract_features.py
    ai/tinyml/.venv/bin/python ai/tinyml/codesize.py     # needs arm-none-eabi-gcc

and the shipped model, straight from the labelled captures in the tree:

    FEATURES=ai/tinyml/captures-door-2026-08-07.csv DEPTH=3 \
        ai/tinyml/.venv/bin/python ai/tinyml/gen_model.py

`captures-door-2026-08-07.csv` is the only training set here that is **not**
regenerable from a public download, so unlike the eWINE features it is tracked. It
came from raw `[ALAB]` capture logs via:

    ai/tinyml/.venv/bin/python ai/tinyml/parse_alab.py \
        --clear <clear.log> --blocked <blocked.log> --blocked <blocked2.log> \
        --frame-len 0 -o ai/tinyml/captures-door-<date>.csv

CSV and not `.npz` because `security/`'s `mal-diff` gate blocks binary blobs from
the tree, correctly: semgrep cannot parse one and a reviewer cannot read one. It is
written at 12 significant figures rather than a display precision, so the generated
scaler is bit-identical whether it is fitted from this file or from the float64
arrays the parser held in memory. At 6 figures it was not, which is harmless for
the model and the wrong property for a provenance file.

## Decision

**Ship an emlearn single decision tree, not TFLM.** On the DW3000-portable feature set a
depth-4 tree scores **0.8625, and cross-compiled for the CDK's Cortex-M4F it is 126 B of
code, 112 B of scaling constants and zero RAM** (Result 6). The best int8 MLP scores
0.8662 at 2,440 B of payload before its interpreter, and the best random forest 0.8684 at
30,802 B. Buying those last 0.6 accuracy points costs an order of magnitude in payload for
the MLP, plus the whole TFLM interpreter (~2 KB core before kernels, ~4 KB of stack for
`AllocateTensors()`, and an arena), or 129x for the forest. Nothing in the accuracy column
justifies either.

**What actually ships is smaller and trained on this hardware.** Result 7 cut the feature
set to the four the DWM3001CDK can produce, Result 9 refit the model on captured walk-ups,
and **Result 11 replaced first-path power with its free-space residual**, `fp_pwr +
20·log10(d / 1 m)`. What ships is a **depth-2 tree on two features**, scoring **0.8800 ±
0.0251** pooled and **0.7729** with a whole capture held out, against **0.7431** and
**0.6381** for Decawave's own rule and the four absolute features on the same splits.
**Result 12 added the on-target feature extractor**, so the module now spans registers to
class rather than starting from features someone else computed. All of it is **776 B** of
flash, no RAM, and no undefined symbols. The eWINE numbers above are the bake-off record,
not the shipped constants.

For scale: the finished classifier is **1.4% of the shipping image's free flash** and none
of its free RAM.

This reverses the research document's primary recommendation, on measurement.

## Setup

- Data: eWINE `UWB-LOS-NLOS-Data-Set`, 42,000 samples, 21,000 LOS / 21,000 NLOS,
  7 indoor locations, DW1000, 1016-tap CIR. Counts confirmed against the upstream README.
- Split: stratified 29,400 train / 6,300 val / 6,300 test, seed 42.
- Trees train on int16-quantised features, so the emlearn int16 conversion is lossless
  rather than an approximation applied afterwards.
- Payload bytes counted exactly and architecture-independently: trees as
  `n_nodes*8 + n_roots*4 + n_leaves` (`EmlTreesNode` is `int8 + 3*int16` = 8 B aligned),
  MLPs as the `.tflite` flatbuffer, both plus `n_features * 2 * float32` of scaling
  constants. Runtime code size is not part of that accounting; `codesize.py` measures it
  separately with a real cross-compile, and Result 6 has the numbers.

Three feature sets were run:

| set | features | CIR window | why |
|---|---|---|---|
| full | 17 | 155 taps | upper bound: everything DW1000 offers |
| **dw3000** | **14** | **64 taps** | **what this project's hardware can actually produce** |
| shape | 9 | 64 taps | power-free: tests how much is really channel shape |

## Result 1: three of the obvious features do not exist on this hardware

The DW3000 `dwt_rxdiag_t` (`deps/dw3000/dwt_uwb_driver/deca_device_api.h:1035`) has **no
noise fields at all**. `STDEV_NOISE` and `MAX_NOISE` are DW1000-only, so `stdev_noise`,
`max_noise` and the derived `fp_snr` cannot be computed on a DW3110 and must be dropped.
In the full-feature run `fp_snr` ranked 6th of 17 by importance, so this is not free: it
costs about 1.5 accuracy points (0.8803 -> 0.8662 for the best MLP, 0.8698 -> 0.8625 for
dtree-d4). Everything else maps 1:1 — `ipatovF1/F2/F3` -> `FP_AMP1/2/3`, `ipatovPower` ->
`CIR_PWR`, `ipatovAccumCount` -> `RXPACC`, `ipatovFpIndex` -> `FP_IDX`.

Training on the DW1000 set as shipped would have produced a model that cannot be fed on
the target — the train/serve skew failure mode, caught before it cost anything.

## Result 2: the data collection path already exists

`modules/woz_uwb/src/driver/uwb_cirdiag.c` already latches `dwt_rxdiag_t` and reads
`CIRDIAG_CIR_WIN = 64` Ipatov taps centred on the first-path index, emitting
`[ALAB] ev=uwb.diag` (with `ipfp ippk ippw ipf1 ipf2 ipf3 ipac`) and `[ALAB] ev=uwb.cir`
lines for `aliro_lab.py`'s k=v parser. Its own header comment gives the motive as "the
leading edge + early multipath that separates inside/outside a door".

The 64-tap window is why the dw3000 feature set above uses `WIN_PRE=32 WIN_POST=32`. One
difference to carry into the on-target extractor: the firmware emits complex taps
(`re=`/`im=`), while eWINE ships magnitudes, so the target must compute
`sqrt(re^2 + im^2)` before the shape features to match training.

## Result 3: the numbers

DW3000-portable feature set (14 features, 64-tap window) — the decision table:

| model | test acc | payload B | acc per KB |
|---|---|---|---|
| rf-n20-d8 | 0.8684 | 30,802 | 0.03 |
| rf-n10-d8 | 0.8673 | 15,218 | 0.06 |
| mlp-8 (int8) | 0.8662 | 2,440 | 0.36 |
| mlp-16x8 (int8) | 0.8660 | 3,704 | 0.24 |
| mlp-32x16 (int8) | 0.8637 | 4,992 | 0.18 |
| dtree-d6 | 0.8632 | 598 | 1.48 |
| **dtree-d4** | **0.8625** | **238** | **3.71** |
| dtree-d8 | 0.8559 | 1,630 | 0.54 |
| dtree-d12 | 0.8417 | 6,758 | 0.13 |
| pwr_diff > 6 dB (vendor rule) | 0.6668 | 0 | - |

Notes:
- **The vendor rule of thumb is worth 0.667.** Decawave APS006's ">6 dB implies NLOS"
  gets two thirds right on a balanced set. ML is worth roughly **20 points** over it, and
  that gap, not the 0.6 points between model classes, is the reason to do this at all.
- **Trees stop improving past depth 6 and then overfit** (d8 0.8559, d10 0.8463,
  d12 0.8417). Depth 4-6 is the whole useful range.
- **Forests buy almost nothing here.** rf-n20-d8 beats dtree-d4 by 0.6 points for 129x
  the bytes.
- int8 post-training quantisation is nearly free for the MLPs: 0.8662 int8 vs 0.8665
  float32 for mlp-8. Quantisation-aware training is not needed.

## Result 4: most of that accuracy is a power measurement, not channel shape

Dropping every absolute-power feature and keeping only shape (`pwr_diff` as a dB ratio,
`peak_over_fp`, `peak_delay`, `mean_excess_delay`, `rms_delay_spread`, `kurtosis`,
`skewness`, `rise_time`, `late_early_ratio`) collapses accuracy from ~0.863 to ~0.74:

| model | test acc (sklearn) |
|---|---|
| rf-n20-d10 | 0.7533 |
| mlp-8 (int8) | 0.7484 |
| dtree-d8 | 0.7421 |
| dtree-d6 | 0.7340 |
| dtree-d4 | 0.7192 |

Feature importance says the same thing: `cir_energy` 0.201, `rx_pwr` 0.198, `fp_pwr`
0.190, `cir_max` 0.174 — four power terms carry 76% of the total.

**Why this matters for a door.** In this data set LOS and NLOS captures span many ranges,
so "weak signal" correlates with "obstructed" partly for reasons of distance rather than
obstruction. A door reader sees a roughly fixed geometry where that shortcut is much
weaker. The honest expectation for on-device accuracy is somewhere between 0.74 and 0.86,
and it cannot be pinned down without captures from the actual install. Treat 0.86 as an
optimistic ceiling, not a forecast.

**But the confound may cut the other way at a fixed door**, and this is worth stating
because it is the difference between "0.86 is inflated" and "0.86 is the wrong task". Here
the power term is partly a distance proxy learned across 7 rooms. At one doorway distance
is roughly constant across both classes, so the surviving power delta is the door itself —
the physically correct signal, not a shortcut. Read 0.74-0.86 as a planning bracket rather
than a ceiling, and do not be surprised if per-install captures beat the public-set number
outright. Only real captures settle it.

## Result 5: emlearn's C is faithful for single trees, not for forests

The bake-off checks the generated C against sklearn on the full test set, per model.
Verified in an isolated re-run:

| model | sklearn | emlearn C | mismatches / 6300 |
|---|---|---|---|
| dtree-d4 | 0.7192 | 0.7192 | 0 |
| dtree-d6 | 0.7340 | 0.7340 | 0 |
| dtree-d12 | 0.7265 | 0.7262 | 6 |
| rf-n5-d6 | 0.8624 | 0.8611 | 60 |
| rf-n10-d8 | 0.8673 | 0.8652 | 75 |
| rf-n20-d8 | 0.8684 | 0.8675 | 70 |

Single trees reproduce essentially exactly; the handful of mismatches are threshold-
equality tie-breaks. **Forests diverge on about 1% of samples** — consistent with emlearn
majority-voting where sklearn averages probabilities. So a forest cannot be validated
against its sklearn parent, which is a second, independent reason to prefer the single
tree: it is the only option whose on-target behaviour can be certified against the
training-side model.

> **Result 14 revises the cause.** The 1% is emlearn's *default*, not its architecture:
> `leaf_bits=8` brings it to 0.06%. The conclusion above stands — 0.06% is still not the
> zero gate 1 requires — but the reason changed, and so does where a future attempt
> should start.

**Rule, for whoever later wants a forest** — *corrected, see Result 14.* The paragraph
that stood here called the divergence "a design difference, not a bug" that "does not go
away with more care", and said the generated C must therefore become the oracle. **The
mechanism was right and the conclusion was overstated.** emlearn takes a `leaf_bits`
argument that switches the generated code from `forest_predict_majority_func` to
`forest_predict_proportions_func`, and it defaults to 0 — majority voting — for
classifiers. Measured, it cuts the divergence 14×, from 0.92% to 0.06% on the same 6,300
held-out samples.

It still does not reach zero, so the *practical* conclusion survives: gate 1 demands exact
agreement, 4 of 6,300 is not zero, and a forest remains uncertifiable against its parent.
But it fails by 4 samples for a quantisation reason, not by 58 for an architectural one,
and anyone revisiting this should start from `leaf_bits=8` rather than from "sklearn is
demoted to advisory". Full numbers in Result 14.

**Harness trap, worth remembering.** An earlier run reported 6300/6300 mismatches for two
tree configs. That was not emlearn's inference: two bake-offs were running concurrently
from the same working directory, and emlearn compiles its Python-callable extension to a
fixed path — `name = 'mytree'` (`emlearn/trees.py:748`) under `temp_dir='tmp'`
(`emlearn/common.py:161`), i.e. `./tmp/mytree.{c,h,o}` — **regardless of the name passed
to `save()`**. So the 9-feature process loaded the 14-feature process's compiled
extension. This is not a same-model-name hazard: *any* two concurrent emlearn processes
sharing a CWD collide. Run them sequentially, or give each one its own CWD or `temp_dir`.
The failure was silent and looked exactly like a model bug.

## Result 6: on the target the whole classifier is 238 bytes and no RAM

`codesize.py` cross-compiles the generated C for the nRF52833's Cortex-M4F with the
optimisation the firmware uses (`CONFIG_SIZE_OPTIMIZATIONS=y`, so `-Os`) and sizes the
object. arm-none-eabi-gcc 16.1.0, flags
`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections
-fdata-sections -ffreestanding`. Both methods reproduce the bake-off's accuracies to the
digit, which is also a check that the pipeline still yields the same model.

| model | method | test acc | object flash | after `--gc-sections` | + 112 B scaler | RAM |
|---|---|---|---|---|---|---|
| **dtree-d4** | **inline** | **0.8625** | **180** | **126** | **238** | **0** |
| dtree-d6 | inline | 0.8632 | 636 | 582 | 694 | 0 |
| dtree-d4 | loadable | 0.8625 | 802 | <=542 | <=654 | 0 |
| dtree-d6 | loadable | 0.8632 | 1,162 | <=902 | <=1,014 | 0 |

The depth-4 inline object is four sections and nothing else:

    .text.dtree_d4_inline_tree_0.isra.0    76 B   the tree itself
    .text.dtree_d4_inline_predict          50 B   the entry point
    .text.dtree_d4_inline_predict_proba    50 B   unreferenced, dropped at link
    .text.woz_ml_predict                    4 B   this harness's wrapper, not shipped

- **126 B is exact, not an estimate.** Only two of those four sections are reachable from a
  classification call, and `-ffunction-sections` guarantees the linker can drop the rest.
  The `loadable` figures are marked `<=` because that heuristic is cruder there: a real
  link would also drop `eml_trees_regress*` and the error strings.
- **76 B is 15 Thumb-2 comparisons**, which is exactly what a depth-4 tree should compile
  to, so the number is not hiding anything.
- **`inline` wins outright at this size.** `loadable` pays a flat ~300 B for
  `eml_trees_predict()` and its helpers, which only amortises across several trees, and
  with one tree there is nothing to amortise. Even depth 6 inline beats depth 4 loadable.
- **Zero RAM.** No arena, no interpreter state, no static buffers: `.bss` and `.data` are
  both empty. The 112 B scaler (14 features x 2 x float32 of `.rodata`) is needed by any
  method, including the vendor rule.
- Against the shipping image's 54,332 B of free flash and 20,060 B of free RAM, the
  complete classifier costs **0.44% of the flash headroom and none of the RAM**.
- TFLM's interpreter core alone is put near 2 KB in the literature before any kernel or
  arena, so roughly **eight times the entire tree including its scaler**, to buy 0.4
  accuracy points. That is the caveat this result was written to close.
- Coincidence worth one line: 238 B measured is the same number the bake-off's payload
  accounting predicted for dtree-d4. Different quantities that happen to agree, but it
  means the cheap proxy was not misleading anyone.

**What the shipped module actually costs.** Result 7 later cut the feature set from 14 to
the four the hardware can produce, and the module was regenerated against it; the figures
below are the four-feature build. `modules/woz_ml/src/woz_ml_los.c`, same flags:

| section | B | |
|---|---|---|
| `woz_ml_los_classify` | 152 | quantise 4 features, then call |
| `woz_ml_los_tree_tree_0` | 74 | the tree, 6 integer comparisons |
| `woz_ml_los_tree_predict` | 50 | generated entry point |
| `woz_ml_los_lo` + `woz_ml_los_scale` | 32 | `.rodata`, 4 x 2 x float32 |
| **classifier total** | **308** | 0.6% of the shipping image's free flash |
| `woz_ml_los_vendor` | 20 | only if called |
| `woz_ml_los_disagrees` | 38 | only if called |

The 14-feature build was 390 B. The entire saving is the scaler, 112 B down to 32;
`woz_ml_los_classify` is a loop over `WOZ_ML_LOS_N_FEATURES` and does not change size with
it, and the tree lost 2 B. Standalone the model measures **156 B** (124 B of code after
`--gc-sections` plus the 32 B scaler), against 238 B on 14 features.

`.data` and `.bss` are both zero. The only runtime cost is 28 B of stack for the quantised
feature vector. The quantiser is the larger half of the classifier, which is worth knowing
before anyone tries to shrink the tree: the tree is not where the bytes are.

The libc note: Homebrew's `arm-none-eabi-gcc` ships no C library, so `codesize.py` writes
three stub headers for the `math.h` / `stdlib.h` / `stdio.h` that emlearn's *logging* pulls
in. The tree walk touches none of it, and the inline object compiles byte-identically with
the stubs removed, which is the cross-check that they are not moving the numbers.

## Result 7: the CIR taps are not load-bearing, so the capture image is not needed

Measured 2026-08-07, after the DWM3001CDK capture image turned out to stop the responder
transmitting altogether (`tx0`, no range, no unlock, three sessions; the plain `make build`
image on the same board ranged 22-443 cm and unlocked normally). That raised the question
this section answers: how much accuracy actually depends on reading the CIR accumulator?

Of the 14 DW3000-portable features, four come from `dwt_readdiagnostics()` alone —
`fp_pwr`, `rx_pwr`, `pwr_diff`, `rxpacc`, all derived from `ipatovPower`, `ipatovF1..F3`
and `ipatovAccumCount`. The other ten need the 64-tap window. Same seed, same `.npz`, same
split, `SUBSET=` on `bakeoff.py`:

| feature set | features | `dtree-d4` acc | payload | acc/KB |
|---|---|---|---|---|
| `all` | 14 | 0.8625 | 238 B | 3.71 |
| `scalar+` | 7 | 0.8619 | 182 B | 4.85 |
| `scalar` | 4 | **0.8611** | **158 B** | **5.58** |
| vendor `>6 dB` | 1 | 0.6668 | 0 B | — |

**Dropping every tap-derived feature costs 0.14 accuracy points and saves 80 bytes.** The
model gets cheaper and stays 19.4 points clear of the vendor rule. That is Result 4 seen
from the other side: if ~76% of the importance is absolute received power, and power is a
register scalar, then the channel-shape features were always decoration.

`scalar+` is the optimistic variant: `ipatovPeak` packs the strongest tap's index and
amplitude, so peak height and its offset from the first path are readable without the
accumulator. It is listed for completeness only — eWINE computes those three from taps, and
a register-derived version would need its own derivation to match numerically.

**One finding that qualifies Result 5.** `scalar+` produced 3 C-vs-sklearn mismatches out
of 6,300, where `all` and `scalar` produced 0. So "emlearn's C is exact for single trees"
holds for the sets certified, not universally: int16 scaler quantisation can flip a sample
sitting on a split threshold. This is precisely what `gen_model.py`'s gate 1 is for. The
shipped model is a `SUBSET=all` tree with 0 mismatches and is unaffected.

Consequence: door captures need only the cheap summary path — one diagnostics read per
reception, no `dwt_readcir`, no 4,352 B ring, no drain pacing.

## Result 8: measured on this hardware, +8.8 points over the vendor rule

2026-08-07, DWM3001CDK, an iPhone walking up to a desk. Obstruction is the operator's
own body: phone in a front pocket, back to the reader. 212 clear and 97 blocked
receptions after filtering to `len=0` RFRAMEs, five-fold cross-validated:

| model | balanced accuracy |
|---|---|
| `dtree-d2` | 0.7293 ± 0.0339 |
| `dtree-d3` | 0.7614 ± 0.0395 |
| **`dtree-d4`** | **0.7958 ± 0.0532** |
| `dtree-d6` | 0.7387 ± 0.0698 |
| tuned `pwr_diff` cut (the vendor rule, threshold refit per fold) | 0.7074 ± 0.0335 |

**Depth 4 is again the optimum**, on data with nothing in common with eWINE. Depth 6
overfits 309 samples. That reproduces Result 3's choice independently.

The baseline is the vendor rule with its threshold **refit on each training fold**, not
its literal `>6 dB`. The literal version scores 0.5080 here, which is chance, and that is
an artefact rather than a finding: `parse_alab.py` applies eWINE's DW1000 constants to
DW3000 registers, so the absolute dB scale is offset and a threshold quoted in the
DW1000's units means nothing. Refitting is the honest comparison.

Class separation, clear vs blocked:

| feature | clear | blocked | delta | Cohen's d |
|---|---|---|---|---|
| `fp_pwr` | −75.56 ± 5.08 | −84.44 ± 5.34 | **−8.88 dB** | −1.70 |
| `rx_pwr` | −96.15 ± 2.96 | −99.84 ± 2.81 | −3.70 dB | −1.28 |
| `pwr_diff` | −20.59 ± 3.75 | −15.41 ± 4.54 | **+5.18 dB** | +1.24 |
| `rxpacc` | 49.14 ± 2.15 | 48.48 ± 2.39 | −0.65 | −0.29 |

The direct path loses 8.9 dB while total energy loses 3.7, so the gap between them widens
by 5.2 dB. That is the effect the vendor rule keys on, and the tree beats the rule by
reading it alongside the absolute levels rather than alone.

**`rxpacc` carries nothing** (d=−0.29) and is now the third analysis to say so. Expect it
to drop out when the model is regenerated, taking 8 B of scaler with it.

**Two labelling traps produced two false results before this one, both worth keeping.**

*The frame-mix confound.* The first labelled pair was 94% `len=0` in the clear class and
24% in the blocked one, because the clear capture predated the fix that let rounds
complete: a broken round retries POLLs (`len=0`) while a working one also carries the
Pre-POLL and Final_Data. Pooled, `rxpacc` looked like the best feature at d=−1.01 and
0.800 single-threshold accuracy; split by frame length it collapsed to d≈0.2. It was
measuring frame type. `parse_alab.py` now reports the per-class mix and warns above a 15%
gap in any length's share.

*The exposed phase.* A blocked run is not entirely blocked, because establishing a session
needs signal, so an operator blocking hard enough to matter has to expose the phone first.
Those receptions land in the blocked file and are physically clear. Measured: 28% of
"blocked" samples fell inside the clear inter-quartile range while 30% sat below the clear
5th percentile — one label over two populations. It cost 16 accuracy points (0.6339 vs
0.7958) and read convincingly as a weak feature set.

The fix is a protocol one: **stay blocked for the whole run and let the unlock fail.**
The capture never needed the unlock, only an armed session, and BLE at 2.4 GHz penetrates
a body far better than UWB at 6.5–8 GHz, so the session establishes while the UWB path
stays blocked. The phone does not sulk, because a completed round reporting a long
distance is ordinary operation rather than a failure. `--blocked-window` exists for runs
that cannot be kept clean end to end.

**Caveats specific to this result.** 97 blocked samples is thin and ±0.053 is wide. One
geometry, one desk, one body, one phone. It says the premise holds on this radio; it does
not say 0.796 is what a doorway would give.

## Result 9: retrained on 556 hardware captures, +10.7 points, and the shipped model is now this board's

2026-08-07, same board and same phone, after the protocol fix from Result 8. Four runs:
two kept blocked end to end and two unobstructed, in separate sessions. **556 receptions,
267 clear and 289 blocked**, `len=0` only. This is what `modules/woz_ml/` now ships.

Balanced accuracy, 5-fold cross-validation repeated ten times (50 folds):

| model | balanced accuracy |
|---|---|
| `dtree-d2` | 0.8395 ± 0.0300 |
| **`dtree-d3`** | **0.8505 ± 0.0302** |
| `dtree-d4` | 0.8458 ± 0.0310 |
| `dtree-d5` | 0.8420 ± 0.0295 |
| `dtree-d6` | 0.8308 ± 0.0327 |
| tuned `pwr_diff` cut (the vendor rule, threshold refit per fold) | 0.7431 ± 0.0359 |

**Depth 3, not depth 4.** Results 3 and 8 both chose depth 4; on this board's captures
the optimum moves down one and stays there as samples are added. The differences between
depths 2 through 5 are inside one standard deviation of each other, so this is a weak
preference, and taking the smallest model that is not measurably worse is the tie-break.

Single-threshold baselines, same 50 folds, which is where the model's knowledge actually
lives:

| single feature | balanced accuracy |
|---|---|
| `fp_pwr` | 0.8207 ± 0.0348 |
| `rx_pwr` | 0.7963 ± 0.0333 |
| `pwr_diff` (the vendor rule) | 0.7431 ± 0.0359 |
| `rxpacc` | 0.5597 ± 0.0211 |

**A single threshold on `fp_pwr` gets within 0.03 of the whole tree.** The other three
features together are worth three accuracy points. `rxpacc` is barely above the coin
flip and is the fourth analysis to say it carries nothing here; it stays in the vector
because dropping it saves 8 B and would fork the layout from `parse_alab.py`, which is
not a trade worth making until something else needs the change.

Class separation over the pooled set:

| feature | clear | blocked | delta | Cohen's d |
|---|---|---|---|---|
| `fp_pwr` | −74.96 ± 4.89 | −83.91 ± 5.04 | **−8.95 dB** | −1.80 |
| `rx_pwr` | −96.06 ± 2.96 | −100.45 ± 2.49 | −4.39 dB | −1.60 |
| `pwr_diff` | −21.10 ± 3.54 | −16.54 ± 4.44 | **+4.56 dB** | +1.14 |
| `rxpacc` | 49.36 ± 2.15 | 48.59 ± 2.62 | −0.77 | −0.32 |

**Two obstruction geometries measure as one class.** The blocked captures used different
physical arrangements: the phone held behind the back, and the phone carried in a back
pocket. They are the same distribution, so pooling them is legitimate rather than
convenient:

| capture | geometry | n | `fp_pwr` |
|---|---|---|---|
| `blocked3` | held behind the back | 104 | −84.10 ± 5.56 |
| `blocked5` | carried in a back pocket | 185 | −83.80 ± 4.72 |

Cohen's d between the two is **+0.06**. Training on one geometry and testing on the other
holds up in both directions, and both beat the vendor floor of 0.7431:

| | balanced accuracy |
|---|---|
| train behind-back, test back-pocket | 0.7308 |
| train back-pocket, test behind-back | 0.7681 |

Pooled cross-validation (0.8505) beats either transfer, which is the expected shape: the
model does generalise across geometry, and it does better having seen both. It is
learning body attenuation rather than one session's arrangement.

**The clear side generalises too, and it drifts more than the blocked side does.** The
two unobstructed captures are separate sessions an hour apart, the board untouched
between them:

| capture | n | `fp_pwr` |
|---|---|---|
| `clear3` | 212 | −75.56 ± 5.08 |
| `clear4` | 55 | −72.65 ± 3.13 |

| | balanced accuracy |
|---|---|
| train on `clear3`, test on `clear4` | 0.9256 |
| train on `clear4`, test on `clear3` | 0.7865 |

Both clear the 0.7431 floor, so holding out a whole capture session does not break the
model. But **d = +0.69 between the two clear sessions, about 2.9 dB**, which is roughly a
third of the 8.95 dB obstruction signal the model is reading. Nothing changed but the
hour and where a body stood. That is the strongest argument in this document for
retraining at an install rather than shipping these constants as universal, and it is
also why the asymmetry above is not surprising: `clear4` is a quarter the size and a
third tighter, so a model fitted on it draws narrower thresholds than one fitted on
`clear3`.

**What shipped.** `DEPTH=3` on this data set, through the same three gates: pruning 15
nodes to 11 with 0 of 556 predictions changed, generated C matching sklearn on all 84
held-out samples, and the float32 scaler matching float64 on all 84 (6 of 336 quantised
values one LSB apart, no class changes). 84 golden vectors over 6 leaves. Measured for
Cortex-M4F at `-Os`:

```
woz_ml_los_classify        152 B   unchanged, it loops over N_FEATURES
the tree                   116 B   5 integer comparisons
lo[] and scale[]            32 B
---------------------------------
classifier                 300 B
```

**Caveats specific to this result.** One room, one phone, one person, one day. The
generation gates were sized for eWINE's 42,000 samples and now see 556, leaving 84 in the
held-out split: they still prove the generated C is the model that was trained, over much
less ground. And 0.85 is a bench number from a desk, not a doorway.

## Result 10: a rolling power baseline does not fix session drift, and the reason kills the whole family

Result 9 measured 2.9 dB of drift in mean `fp_pwr` between two unobstructed sessions an
hour apart, about a third of the obstruction signal. The obvious repair is to stop
classifying on absolute power: let the reader keep a causal estimate of its own recent
"normal" and classify on the shortfall below it, so a session-wide offset cancels by
construction. It costs a few bytes of RAM and no flash worth counting.

**It does not work.** The baseline is a decaying running maximum, `base <- max(x, base -
DECAY)`, which an attenuated reception can never pull down faster than `DECAY` and which
follows genuine change at `DECAY` dB per reception. Scored by holding out a whole capture
session, which is the only split that can see the effect at all:

| variant | worst held-out session |
|---|---|
| **absolute powers (what ships)** | **0.7865** |
| shortfall added as a fifth feature | 0.7983 (best of 7 decays, on `rx_pwr`) |
| shortfall added, mixed-class stream | 0.7861 |
| shortfall replacing both powers | 0.6772 (best of 8 decays) |
| shortfall replacing, mixed-class stream | 0.6668 |

Adding it is worth nothing: +1.2 points at best, inside the noise on 344 test samples,
and adding a robust feature never forces a tree to use it. **Replacing is far worse than
doing nothing**, and lands below the 0.7431 vendor floor. Nine decay values from 0 to 5 dB
per reception, and the failure is flat across all of them, which is the signature of a
wrong mechanism rather than a mistuned one.

**Why, and it generalises past this particular baseline.** The running maximum anchors to
the strongest recent reception, and the strongest recent reception is the *closest* one.
So the shortfall mostly encodes "how far am I compared to the nearest I have been lately",
and distance spans 0 to 534 cm inside every capture here. The feature is dominated by a
nuisance variable.

That is the project's own founding observation turned against the fix: an obstructed 1 m
and a clear 3 m produce similar absolute power, which is exactly why this classifier
exists. **A baseline built out of absolute power cannot repair an ambiguity caused by
absolute power.** Any normalisation of the same shape fails the same way.

The first two variants were also scored over single-class captures, where a blocked run
contains only blocked receptions, so the baseline anchors to the strongest *blocked*
reception and normalises the obstruction away. The mixed-class rows exist to rule that out
as the explanation: interleaving the captures into one stream, as a deployed reader would
meet them, changes nothing.

**What this leaves.** The honest figure for an unseen session is **0.7865**, not the
0.8505 pooled number, and at that level the four-feature tree is indistinguishable from a
single threshold on `rx_pwr` (0.7923). The next candidate is not another power
normalisation: it is conditioning on the ranging distance, which the reader already
computes and which no feature in the vector currently carries. That needs the distance
parsed out of the capture logs, which `parse_alab.py` does not do yet.

## Result 11: subtracting free-space spreading is worth +13.5 points, two fewer features and 50 fewer bytes

Result 10 ruled out normalising power against its own history. The remaining candidate was
the one thing the reader already computes and no feature carried: **the ranging distance**.
An obstructed 1 m and a clear 3 m produce similar absolute power, which is this
classifier's founding problem, and it is only a problem while the model does not know
which of the two it is looking at.

The feature is one line of physics, no training and nothing to tune:

    fp_resid = fp_pwr + 20 * log10(d / 1 m)

Free-space spreading is 20·log10(d), so adding it back normalises every reception to what
it would have measured at one metre. What remains is attenuation that distance does not
explain, which is the definition of the thing being detected.

Scored on three splits, strictest first. Leave-one-capture-out holds out an entire
capture, so the model has never seen that session at all:

| variant | leave-one-capture-out | held-out session | pooled CV |
|---|---|---|---|
| absolute, 4 features (ships) | 0.6381 | 0.7195 | 0.8513 ± 0.0319 |
| **`fp_resid` + `rx_pwr`, 2 features** | **0.7729** | **0.7970** | **0.8800 ± 0.0251** |

**+13.5 points on the strictest split, P(better) = 1.00** over 4,000 paired bootstrap
resamples on identical test rows. The gain shrinks as the split gets easier, which is the
right direction: the pooled number was always the one flattering itself.

**Two controls, because the winner has half as many features and that alone could explain
it.** Both are decisive:

| control | leave-one-capture-out |
|---|---|
| `fp_pwr` + `rx_pwr` (same size, no residual) | 0.5895 |
| `distance` alone | 0.1989 |

The same-size absolute pair scores *worse* than the shipped four, so this is not fewer
features overfitting less: swapping `fp_pwr` for its residual at constant feature count is
the entire effect. And distance alone is far below chance, so the improvement is not the
model reading an NLOS range bias as a shortcut to the label.

**Depth 2, not 3.** Under leave-one-capture-out the residual model scores 0.7729 at depth
2, 0.7310 at 3, 0.7011 at 4; pooled cross-validation independently prefers 2 as well.

**Cross-compiled for Cortex-M4F at `-Os`, it is also smaller**: 74 B of tree and a 16 B
scaler, against 108 B and 32 B for the shipped depth-3 four-feature model. Roughly 50 B
less, before the cost of computing the residual.

**SHIPPED, with the logarithm as a table.** `modules/woz_ml/` advertises plain C99 with no
libc and no float division, which `arm-none-eabi-nm -u` can check and which `log10f` would
end. `woz_ml_range.c` computes `log2` from the integer's leading-bit position, one `CLZ`
instruction on Cortex-M4, plus a nine-entry table of `log2(1+x)` with linear interpolation.
Both objects still report zero undefined symbols.

**The table is sized by measurement, not taste.** Rounding the residual to steps as coarse
as 2 dB moves pooled accuracy by +0.0005: the tree's thresholds are simply nowhere near
most samples. Table sizes from 8 to 64 entries all bottom out at the same 0.183 dB maximum
error against float64, because the dominant term is the reader reporting range as a whole
number of centimetres, worth ~0.4 dB at the short end where the curve is steepest. Eight
entries it is.

`gen_model.py` gained **gate 3** for it, holding the table to the same standard gate 2
holds the float32 scaler to: recompute every sample's residual the way the target will,
and refuse to write unless the classification is identical. Currently 0 of 544.

**It costs flash rather than saving it, and the earlier estimate here was wrong.** Measured
for Cortex-M4F at `-Os`: 236 B for `woz_ml_los_classify`, 74 B of tree, 120 B of range
correction and a 36 B table, so **466 B against 300 B** for the four-feature depth-3 model.
`woz_ml_los_classify` grew from 152 B even though it now loops over two features rather
than four, because at two features GCC folds `lo[]` and `scale[]` in as immediates and the
32 B `.rodata` scaler disappears. RAM stays 0 B and stack stays 28 B. 466 B is 0.9% of the
shipping image's free flash, for +13.5 points on the strictest split.

The data is in the tree as `ai/tinyml/captures-door-2026-08-07-ranged.csv`, and
`parse_alab.py --distance` regenerates it. The distance is not on the `[ALAB]` lines: it
comes from the bench TUI's status line, aligned on that line's cumulative reception
counter rather than the wall clock, because probe-rs drains the RTT ring on attach and
stamps the whole burst with one identical millisecond. Twelve of 556 receptions fall
outside any fresh status line and are dropped, which is why the ranged set is 544 rows.

## Caveats

- **No cross-environment evidence, and it is not recoverable from this data set.** The
  obvious hope is that `uwb_dataset_part1..7.csv` are the 7 locations — 7 files, 6,000
  rows each, and the README says "in every indoor location 3000 LOS samples and 3000 NLOS
  samples were taken". They are not. Per-file NLOS counts are 2968, 2982, 2992, 3055,
  2967, 2986, 3050, not 3000 each; the spread matches a hypergeometric draw of 6,000 from
  a globally shuffled 21,000/21,000 pool (sigma 35.9, all seven within 1.6 sigma), and
  per-file mean range (3.78-3.90 m) and mean `CIR_PWR` are near-identical. The README says
  as much: "samples are randomized". So the location label was destroyed upstream, not by
  the merge here, and leave-one-site-out cannot be reconstructed. These numbers say
  nothing about generalising to an unseen doorway.

  This matters less than it looks. A 238 B depth-4 tree does not transfer, it retrains: a
  few hundred locally labelled samples from the install replace the model outright, so
  cross-environment generalisation is a property a shipped universal model would need and
  a per-install retrained one does not. The eWINE model's job was feature validation and
  bake-off substrate, and that job is done.
- **Runtime code size: measured for emlearn (Result 6), still literature-only for TFLM.**
  Comparing them directly would need the TFLM sources cross-compiled here, which is work
  nobody needs while the tree wins by this margin.
- ~~**Different radio.**~~ **Settled by Result 9.** eWINE is DW1000 at PRF 64 / channel 2
  / 110 kbps and this project runs a DW3110 on channel 9, so the eWINE thresholds were
  never shippable constants. They have been replaced: the model now ships thresholds
  fitted on 556 captures from this board. The eWINE numbers above stay as the bake-off
  record, which is the job they were collected for.
- **No golden vectors yet.** `tflite-micro`'s pip wheels are cp310/cp311 + manylinux
  x86_64 only, so the bit-exact TFLM reference-kernel oracle cannot run on this Mac
  (arm64 / py3.12). That work has to happen in CI on Linux. It is moot if the tree wins,
  since emlearn needs no such oracle — the C-vs-sklearn check above is the equivalent.

## Result 12: the extractor ships, and the gate that certifies it recovers its own test data

Everything through Result 11 certified the classifier against features **this generator**
computed in float64. The target does not have those. It has five Ipatov CIA registers, and
between them and a class sits a 64-bit sum of squares, a `2^17` channel-area scaling, a
subtraction of logarithms standing in for a division, and three interpolated logarithms —
none of it covered by gates 1-3. `modules/woz_ml/src/woz_ml_feat.c` is that path.

```
num    = F1^2 + F2^2 + F3^2
fp_pwr = 10*log10(num / C^2)          - 121.74
rx_pwr = 10*log10(area * 2^17 / C^2)  - 121.74
```

**Gate 4: 0 of 544 classifications change, maximum error 0.0099 dB on the powers.** That
is an order of magnitude better than the 0.183 dB gate 3 already tolerates, because the
interpolation error partly cancels between the numerator and denominator terms — both are
logarithms from the same table and the expression subtracts them.

**The gate had to manufacture its own inputs, and that is the part worth reading.** The raw
capture logs are not tracked: `data/` is gitignored and ~1.8 GB. What is tracked is derived
decibels. So the gate inverts the formulas above to recover the integers:

```
num  = C^2 * 10^((fp_pwr + A)/10)
area = C^2 * 10^((rx_pwr + A)/10) / 2^17
```

This is only legitimate if the inversion is lossless, so the gate checks that **first and
separately**, and aborts if it is not. The recovered integers reproduce the recorded
decibels to **4.4e-10 dB**, which is what licenses rounding them to integers. Without that
check the gate could compare the C against numbers the radio never produced and still
report zero mismatches — a gate that passes by construction is worse than no gate, because
it reads like evidence.

**A silent defect found on the way.** The generator's defaults still described the public
eWINE set at depth 4, while the shipped model had been 544 door captures at depth 2 since
`c70cdc1e`. The README's one-line regenerate command therefore **replaced the shipped tree
with a different model trained on different data, and every gate reported zero** — the
gates certify the generated C against whatever was just fitted, not against what ships, so
they are structurally incapable of catching it. The defaults are now the shipped
configuration and a bare run reproduces all four generated files byte for byte, which is
the only form of the check that works.

**Size: 776 B, 0 B of RAM**, linked with `-nostdlib` and `--gc-sections` so the number is
what the linker keeps rather than what the objects contain:

| symbol | bytes |
|---|---|
| `woz_ml_los_classify` | 236 |
| `woz_ml_los_features` | 184 |
| `woz_ml_log2_u64` | 136 |
| `woz_ml_los_tree_predict` | 74 |
| `woz_ml_los_range_correction` | 52 |
| `woz_ml_log2_tab` | 36 |
| `woz_ml_los_disagrees` | 32 |
| `woz_ml_los_fp_resid` | 24 |
| **total** | **776** |

The 32-bit logarithm Result 11 shipped became a 64-bit one shared by both callers, costing
92 B: `rx_pwr`'s numerator is a 17-bit area shifted left by 17, which does not fit in 32
bits, and the sum of three squares does not either. Keeping a second specialised copy would
have cost more than sharing one general one. `woz_ml_los_tree_predict_proba` is generated by
emlearn, called by nothing, and dropped by `--gc-sections`; it is in the objects and not in
this table, which is the difference the link measures.

**What this does not do.** It still classifies *obstruction*, not *side of the door*. One
antenna estimates how much material is in the path; which side of a plane the phone is on
is a different quantity and no channel feature recovers it. The two-anchor work is where
that question is answered. This result closes the gap between "a model exists" and "the
firmware can feed it", nothing more.

## Result 13: round-to-round variance is real, unproven, and cannot be tested on this data

An external proposal argued that variance should separate a body from a door: a standing
person sways and breathes so a torso shadow flickers, while a door is rigid. It is the best
idea in that document and the only one not already built here, so it was tested.

**This data cannot answer the question it asks, and that has to come first.** There is no
door in these captures. `clear` is the phone in hand facing the reader; `obstructed` is the
phone behind the back or in a back pocket. Both classes are a body. What can be tested is
the premise underneath: does variance carry obstruction information at all. And the test is
**biased towards the hypothesis**, because the obstructed class here is exactly the
flickering case the proposal says variance detects. A null result is therefore strong
evidence against it; a positive result is weak evidence for, and would need a real door.

**The information is there, and it is not something the model already has.**

| measure | result | reading |
|---|---|---|
| `pwr_diff.absdiff9` effect size | **d = 0.954** | large, real separation |
| `pwr_diff.std5` effect size | d = 0.709 | ditto |
| solo balanced accuracy | 0.63 – 0.69 | informative on its own |
| predicting `pwr_diff.std5` from `[fp_resid, rx_pwr]` | **out-of-block R² = −0.202** | worse than predicting the mean: genuinely new |

**It still earns nothing.** Adding one variance feature to the shipped pair:

| model | best delta | verdict |
|---|---|---|
| depth-2, what ships | **+0.0000** | the tree never selects it |
| depth-4 | +0.0281 (`pwr_diff.std5`) | 95% CI **[−0.0116, +0.0644]** |
| random forest, 200 trees | +0.0312 (`fp_resid.std9`) | signs flip across adjacent window sizes |

Paired bootstrap over blocks, 2,000 draws: P(better) = 0.912, CI includes zero. Controls
behaved — distance alone 0.6997, shuffled labels 0.4947.

**Testing it at depth 2 alone would have been rigged.** A depth-2 tree has three nodes and
can simply decline a third feature, so a +0.0000 delta there measures whether the shipped
tree changes its mind, not whether the information exists. Both questions are asked above
because they have different answers.

**Two methodological traps, either of which would have manufactured a result.** Rolling
features make neighbouring rows share inputs, so a random split lets the test set read its
own neighbours through the window; every split here holds out contiguous blocks and drops
rows whose window straddles a boundary. And the bootstrap resamples **blocks**, not rows —
resampling rows would have produced a far tighter interval than the data supports and turned
a null into a finding.

**Verdict: not shippable, and the measurement that would settle it is one this set cannot
provide.** The subject walked — median 25.6 cm of movement between consecutive receptions —
so motion contaminates every variance estimate, which is why the variance is taken over
`fp_resid` with spreading already removed rather than over raw power. What the hypothesis
needs is a **stationary dwell**: a person standing still with the phone pocketed, against a
real closed door, at matched distances. Until that exists this stays a good idea with an
effect size and no proof.

## Result 14: the forest's 1% was a default, not an architecture — and it still fails the gate

Result 5 concluded that a converted RandomForest cannot be certified against sklearn,
because the generated C majority-votes where sklearn averages probabilities. That
mechanism is real, but calling it a property rather than a default was wrong.

emlearn's `convert()` takes **`leaf_bits`**. At `trees.py:553` the generated predictor is
`forest_predict_majority_func if leaf_bits == 0 else forest_predict_proportions_func`, and
at `trees.py:704-708` `leaf_bits` defaults to `None`, which becomes **0 for classifiers**
and 32 for regressors. So the sklearn-matching path was always there, one keyword argument
away, and every forest measured in Result 5 was built with soft voting switched off.

Measured on emlearn 0.23.2, 20 trees at depth 6, the same splits as gate 1:

| `leaf_bits` | door captures (82 held out) | public eWINE (6,300 held out) |
|---|---|---|
| **0** — the classifier default | 1 (1.22%) | 58 (**0.92%**) |
| 8 | 0 | **4 (0.06%)** |
| 6 | 0 | 4 (0.06%) |
| 4 | 0 | 10 (0.16%) |

0.92% reproduces Result 5's "about 1%" almost exactly, which is the check that says this
is the same phenomenon and not a different one.

**The conclusion survives; the reason does not.** Gate 1 demands exact agreement, and 4 of
6,300 is not zero, so a forest still cannot be certified against its parent. But it now
fails by four samples for a quantisation reason instead of by fifty-eight for an
architectural one, and the difference matters to whoever revisits this: start at
`leaf_bits=8`, not at "the generated C becomes the oracle".

**The residual is a floor, not a resolution knob.** `quantize_probabilities_into_byte`
(`trees.py:26`) puts each leaf's probabilities on 255 steps, so near-ties can still flip.
More bits do not buy it off — 6 gives the same 4 as 8, and 4 gives 10 — so the error is
where the leaves land relative to the tie, not how finely they are stored.

**None of this changes what ships.** A forest was rejected on bytes first: 30,802 B against
the tree's 466 B at the time, for at best +0.4 accuracy points. Certifiability was the
second, independent reason, and it is now a smaller reason rather than an absent one.
`ai/tinyml/leaf_bits.py` reproduces the table.

## Result 15: a running mean of `fp_resid` removes the offset it is meant to, and still buys nothing

Result 10 ruled out normalising received power against a causal running baseline, and its
reason was mechanical rather than empirical: such a baseline anchors to the strongest
recent reception, the strongest recent reception is the closest one, so the shortfall
mostly re-encodes distance. That reason cannot reach `fp_resid`, which has free-space
spreading at the measured range already subtracted (Result 11). So the family was closed
by an argument that does not apply to its best member, and this is the test of the member.

**The biggest number in the run is a label oracle, and it has to lead**, because anyone
re-running this will meet it first. Computing the baseline over a single-class stream
gives **+0.1026** on a random forest, three times the largest effect in Result 13:

| stream | w | rows | d | solo | +depth2 | +depth4 | +forest | replaces |
|---|---|---|---|---|---|---|---|---|
| 1-class | 5 | 464 | 0.018 | 0.4993 | +0.0000 | +0.0614 | **+0.0891** | -0.0701 |
| 1-class | 9 | 400 | -0.017 | 0.5038 | +0.0000 | +0.0473 | **+0.1026** | -0.0955 |
| 1-class | 17 | 272 | -0.124 | 0.5128 | -0.0251 | +0.0684 | +0.0665 | -0.1368 |
| 1-class | 25 | 144 | -0.185 | 0.4242 | -0.0018 | +0.0229 | +0.0467 | -0.0661 |
| mixed | 5 | 504 | -1.769 | 0.8164 | -0.0273 | +0.0082 | +0.0215 | -0.0276 |
| mixed | 9 | 472 | -1.627 | 0.8075 | -0.0147 | +0.0003 | +0.0122 | -0.0210 |
| mixed | 17 | 408 | -1.490 | 0.7970 | +0.0000 | -0.0029 | +0.0142 | -0.0226 |
| mixed | 25 | 344 | -1.665 | 0.8279 | +0.0000 | -0.0088 | +0.0055 | -0.0199 |

The feature scores 0.4993 alone, chance, and adds 0.1026 in company. That gap is the tell. `fp_resid` minus its own shortfall **is** the trailing local mean, and a model
holding both columns can recover it whenever it helps; in a single-class block that local
mean is a class average. Scored alone, on the same folds:

| stream | w=5 | w=9 | w=17 | w=25 |
|---|---|---|---|---|
| 1-class | 0.8849 | 0.9331 | **0.9724** | 0.9180 |
| mixed | 0.6183 | 0.5824 | 0.5345 | 0.5015 |

against 0.8310 for `fp_resid` itself on all rows. The recovered baseline beats the
strongest shipped feature, and **sharpens as the window lengthens**, which is what a class
mean does when given more samples and what no real channel feature does. In a mixed stream
it decays to 0.5015. This is not train/test leakage, which the blocked split already
stops; it is the label reaching the model through the stream's own composition, and no
deployed reader meets one class at a time.

**The mechanism works. That is the part worth recording.** Measured without a classifier
in the way, as the spread of per-block mean `fp_resid` within a class:

| stream | w | clear | obstructed |
|---|---|---|---|
| none | - | 2.822 | 1.274 |
| 1-class | 5 | **0.439** | **0.603** |
| 1-class | 25 | 1.538 | 2.606 |
| mixed | 5 | 1.640 | 1.772 |
| mixed | 25 | 0.863 | 2.014 |

A short causal mean cuts the between-block offset in the clear class by a factor of 6.4.
Result 10's objection genuinely does not apply here: nothing re-encodes distance.

**It fails for a second reason instead, and that one does generalise.** The offset the
baseline removes and the level that carries the class are the same quantity. Over a mixed
stream the baseline becomes a local blend of both classes, so the shortfall collapses into
a shifted copy of its own input: d = -1.769 and 0.8164 solo, against `fp_resid`'s own
0.8310. It is not a new view of the channel, it is the old one minus a near-constant, and
the shipped depth-2 tree is worse with it (-0.0273) rather than indifferent. Replacing
`fp_resid` with it costs 0.02 to 0.14 everywhere, reproducing the shape of Result 10's own
replacing variants.

**Both streams are artefacts, in opposite directions**, and neither is the deployed case.
The single-class stream hands over a class average. The mixed stream alternates the two
classes reception by reception, which is the most uninformative baseline available and
flatters the null. A real reader meets runs of one class lasting seconds, somewhere
between the two, and the tracked CSV cannot say where: it carries no session or run
structure, only `label`. As in Result 13, the honest summary is that the data answers a
neighbouring question rather than the one asked.

One thing this run does settle in the proxy's favour. Between-block spread of mean
`fp_resid` in the clear class is 2.822 dB, comparable to the 2.9 dB/h session drift of
Result 9. Blocks standing in for sessions carry offsets of the size the idea was built to
remove, so the null is not an artefact of the proxy being too gentle.

`ai/tinyml/variance.py` reproduces every number above, including the oracle control, which
is there so the 1-class column cannot be read without it. The code landed inside
`0f01caea`, whose message describes only the `leaf_bits` work of Result 14; this Result is
what that code was for.

## Result 16: the line loses the decision by 5.5 points and wins the confidence outright

Two axis-aligned steps against one oblique line, same two features, same folds. The tree
wins and it is not close:

| model | bAcc |
|---|---|
| **depth-2 tree, what ships** | **0.8773** |
| depth-4 tree | 0.8705 |
| one threshold on `fp_resid` | 0.8328 |
| linear SVM | 0.8239 |
| LDA | 0.8238 |
| logistic regression | 0.8222 |
| one threshold on `rx_pwr` | 0.7859 |
| shuffled-label control | 0.4318 |

So a linear model is not a candidate to replace the tree, and that closes the question as
asked. It opens a better one. `Next` item 6 has been blocked on a quantity that does not
exist: `woz_ml_los_tree_predict_proba()` writes 1.0f into one slot and 0.0f into the
other, because a single tree's leaf **is** the class. The module can say "obstructed" and
cannot say how sure, and a door deciding what a wrong answer costs needs the second thing.
A line has it for free as the signed distance from the boundary.

**The hybrid question is not whether the margin grades the line's own answers, it is
whether it grades the TREE's.** The tree would still decide. Out-of-fold, binned by
quartile of each candidate confidence, against the tree's own correctness:

| confidence | Q1 | Q2 | Q3 | Q4 | monotone |
|---|---|---|---|---|---|
| \|linear margin\| | 0.7721 | 0.8162 | 0.9338 | **0.9853** | yes |
| tree leaf purity | 0.7890 | 0.9389 | 0.8514 | 0.9103 | **no** |

The margin separates the answers the tree gets right 98.5% of the time from the ones it
gets right 77.2% of the time. It is the only quantity measured here that does.

**The cheap alternative was measured and it does not work**, which matters because Result
14 makes it the obvious thing to reach for. A depth-2 tree has four leaves and each leaf
carries a training-set purity, which is exactly what emlearn emits at `leaf_bits=8`, one
byte per leaf per class, no multiplies at all. Purity is non-monotone: Q2 at 0.9389 sits
above Q3 at 0.8514. `leaf_bits` fixes forest certification (Result 14) and does not supply
a single tree with a confidence.

**The shipping form needs no libc and no scaler.** Folding the standardiser into the
coefficients is exact, so the target multiplies the decibels `woz_ml_los_features()`
already produces:

```
obstructed  <=>  -0.174858*fp_resid -0.400663*rx_pwr -52.466789  >  0
  folded form vs sklearn, 544 rows:  0 disagreements
  float32 vs float64, 544 rows:      0 disagreements
  margin ordering == probability ordering: True
```

The last line is what keeps `arm-none-eabi-nm -u` clean. A logistic **probability** needs
`exp()`; the margin is monotone in it, so every threshold on the probability is a
threshold on the margin, and the exp stays on the host. On target it is two multiplies and
an add on the M4F's FPU.

**Free side effect.** Tree and line disagree on 53 of 544 receptions, 9.7%. That is a
second unlabelled drift monitor of the same shape as `woz_ml_los_disagrees()`, and it
watches a different axis: the vendor rule disagreement moves when the install changes, a
tree-versus-line disagreement moves when the boundary shape stops fitting.

**It costs 56 bytes**, and the figure is measured on `modules/woz_ml/src/woz_ml_lin.c`
itself rather than on a copy, so it cannot drift from what a firmware build links.
Cross-compiled for the nRF52833's Cortex-M4F at `-Os` with the flags `codesize.py` uses:

| section | size |
|---|---|
| `.text.woz_ml_los_confidence` | **56 B** |
| `.rodata` / `.data` / `.bss` | 0 B |

`arm-none-eabi-nm -u` reports no undefined symbols, which is the libc-free claim checked
the way `modules/woz_ml/README.md` checks it, and is why the confidence is a margin rather
than a probability: `exp()` would have ended it. The coefficients cost no `.rodata`, since
`static const` in the one translation unit that reads them folds into the code. Against
the tree's 74 B `woz_ml_los_tree_predict` and the module's 776 B, the confidence is 0.1%
of the shipping image's 54,332 B of free flash and no RAM at all.

A hand-rolled version taking two floats and returning the signed margin measures 40 B. The
16 B difference buys the array signature `woz_ml_los_classify()` already uses, the loop
over `WOZ_ML_LOS_N_FEATURES` rather than two named parameters, and the absolute value that
makes the sign unreachable. That last one is the point rather than an overhead: the sign
is the linear model's own class, and shipping it would hand callers a 0.8222 classifier
next to a 0.8773 one.

**What this does not decide.** A quarter of receptions land in Q1, where the tree is right
77.2% of the time. What a lock does with a low-confidence obstruction call is exactly the
question `Next` item 6 exists to answer, and supplying the confidence does not answer it.
The absolute levels are blocked CV rather than a session hold-out, optimistic for the same
reason as every number since Result 13; the comparison across models on identical folds is
not. And the margin is computed in the same two-feature space the tree splits, so "far
from the line" and "far from the tree's thresholds" are correlated by construction. That
is the mechanism by which it works, not a flaw in the measurement, but it is why the
margin grades this tree and would need re-fitting beside any other one.

`ai/tinyml/linear.py` reproduces all of it. Nothing calls any of this: no linear model is
generated into `modules/woz_ml/`, and `CONFIG_WOZ_ML_LOS` is still default n.

## Result 17: an obstructed phone reads much farther than it is — superseded by Result 18

Measured on this board, standing on a tape-measured spot, one run with the phone in hand
facing the reader and one with it in a back pocket and the body between:

| condition | median range | mean | IQR | rx_pwr | n |
|---|---|---|---|---|---|
| hand | **105 cm** | 103.0 | 13 | -93.59 dB | 69 |
| pocket | **190 cm** | 168.2 | 46 | -101.87 dB | 65 |

**pocket minus hand: +85.0 cm, 95% CI [+70.0, +93.0]**, bootstrapped over medians.

`unlock_cm` is 100. **So the pocketed owner never crosses the unlock threshold at all**,
and the ordinary walk-up -- phone in a pocket, which is how a phone is usually carried --
is the case the lock is worst at. That is a usability failure with a number on it rather
than a suspicion.

**The hand run is the control that makes the rest readable.** It lands at 105 cm against a
100 cm tape, so the reader's absolute range is good here and the uncalibrated antenna
delay this project has never programmed is small at this distance. The difference is
between two runs at one spot, so that constant cancels regardless.

**It is not the subject moving, and the evidence is a speed.** The final transition out of
the pocketed segment is 197 cm to 96 cm across consecutive receptions **0.354 s apart**,
which is **2.85 m/s**. Nobody standing on a tape mark does that. At the same reception
`ipatovPower` steps 3 to 12, a 4x power jump, which is what a phone leaving a pocket looks
like and not what walking looks like. The entry transition is weaker evidence -- 163 to
205 cm over 0.587 s is 0.72 m/s, an ordinary walking pace -- so the exit is what carries
the claim.

**A second, independent argument from the power.** rx_pwr falls 8.3 dB between the runs.
A genuine move from 105 cm to 190 cm is worth only 20*log10(190/105) = 5.2 dB of
free-space spreading. The extra ~3 dB has to be attenuation through a body, which is
consistent with a subject who did not move and inconsistent with one who did.

**The mechanism is the ordinary NLOS one.** With the direct path attenuated, the CIA's
first-path estimate latches onto a later arrival, so the time of flight is measured to a
reflection rather than to the phone. The range inflates; it does not shrink. That
direction matters for the lock: an obstruction makes the phone look further away, never
closer, so compensating for it cannot manufacture proximity that the radio did not see.

**This is what settles how the classifier should be wired.** RESULTS `Next` item 6 asked
what a wrong answer costs at a door, and the honest answer had been that nobody knew. It
is now bounded: the classifier's job is not to decide whether to trust someone, it is to
say "this reception is obstructed, so the reported range is inflated". Compensation, which
ADDS permission by undoing a measurement error, and which never touches the STS quality
check that defends against a distance-reduction attack (`docs/range-integrity.md` layer
2). A phone that looks 85 cm too far is an owner locked out; a phone that looks closer
than it is cannot be produced this way.

### Correction: +85 cm is an UPPER BOUND, not the bias

Recorded immediately after the result above and before anything was built on it. The
subject clarified what the exit transition actually was: **a turn in place**, which put
the back pocket in line of sight where previously a thigh was in the path. That is better
than the walking hypothesis this Result rejected -- a rotation holds the subject's
position -- but it exposes a confound the experiment never controlled.

**A hand-held phone and a back-pocket phone are not at the same distance from the reader
even when the subject stands on the same mark.** Held out in front, the phone sits perhaps
20-30 cm nearer than the body centre; in a back pocket it sits 20-30 cm further, and
turning around swaps the sign. So of the +85 cm, something like **40-60 cm may be
geometry** rather than channel. Nothing in this capture separates them.

The same correction applies to the power argument, and weakens it by the same mechanism. A
genuine 40 cm offset at this range is worth 20*log10(145/105) = 2.8 dB, so of the 8.3 dB
measured, roughly 5.5 dB is body attenuation rather than the ~3 dB claimed above.

**What survives, and it is still the finding.** The DIRECTION is not in doubt: obstruction
inflates the reported range, both the range and the power say so, and the physics has no
mechanism to shrink it. The MAGNITUDE is now an upper bound of 85 cm with a plausible
channel component nearer **45 cm**, which is still most of the way to `unlock_cm` and
still enough to lock out a pocketed owner. What it is not is a number to size a correction
from.

**The experiment that removes the confound**, and it is cheap: leave the phone at a fixed
point -- a stool or a tripod on the tape mark -- and move only the SUBJECT, standing
between it and the reader for one run and out of the path for the other. The phone never
moves, so every centimetre of difference is channel. That is what should have been asked
for the first time.

**Caveats, and the first is the one that would overturn this.**

- The subject reports having "mostly stayed there", which is not the same as not having
  moved. The 2.85 m/s exit and the 8.3 dB power drop are what stand in for a
  motion-capture rig, and both point the same way, but neither is a position measurement.
  See the correction above: the exit was a turn, and the turn moves the phone.
- **One distance only.** Whether the bias is a constant 85 cm or scales with range is
  unmeasured, and the two lead to different corrections. A second pair at 200 cm answers
  it in two minutes.
- One geometry, one subject, one phone, one pocket, one session. 69 and 65 receptions.
- `parse_alab.py` flags that the two runs differ in **frame mix** (blocked carries 28%
  `len=58` against 7% for clear), so they did not see identical traffic. It matters more
  to a fitted classifier than to a median range, but it is not nothing.
- The shipped model called 89.2% of the pocketed receptions obstructed against 21.7% of
  the hand ones, which is the first field evidence that a tree fitted on a walking subject
  still separates a pocket when the subject stands. It is a sanity check, not an accuracy
  measurement: these captures are its test set and its training set at once.

`ai/tinyml/captures-pocket-1m-2026-08-07.csv` is the data;
`ai/tinyml/range_bias.py` reproduces every number. This is also the first capture whose
range came from the firmware's own `d=` field rather than from scraping the bench TUI's
rendered status line, which is what made a standing capture possible at all.

## Result 18: with the phone bolted down, a body in the path adds 80 cm

Result 17 measured +85 cm and then corrected itself down to "an upper bound, plausibly
nearer 45 cm", because a hand-held phone and a pocketed one are not at the same distance
even when the subject stands on one mark. **That correction was too pessimistic and this is
the controlled repeat that says so.**

The phone sat on a tripod and was not touched between runs. Only the subject moved: out of
the path for one run, squarely in it for the other. Every centimetre of difference is
therefore channel, because nothing else changed.

| condition | median | mean | IQR | rx_pwr | fp_pwr | n |
|---|---|---|---|---|---|---|
| clear | **75 cm** | 71.2 | **7** | -92.7 dB | -70.5 dB | 107 |
| blocked | **155 cm** | 160.2 | 38 | -99.5 dB | -82.0 dB | 63 |

**blocked minus clear: +80.0 cm, 95% CI [+72.0, +92.0]**

Against Result 17's uncontrolled +85.0 cm [+70, +93]. **The geometry confound was worth
about 5 cm, not the 40-60 cm the correction feared**, and the honest lesson is that the
correction was as unmeasured as the thing it corrected. Both are now superseded by a
number that did not need either assumption.

**The clear run's IQR of 7 cm is the receipt that the setup held.** A phone that is not
moving, with line of sight, reports the same distance to within a few centimetres for two
minutes. The blocked run's IQR of 38 cm is five times wider, which is the first-path
estimate wandering between arrivals rather than the phone wandering around the room.

**11.5 dB of first-path attenuation** (`fp_pwr` -70.5 to -82.0) with the phone stationary,
and no free-space component to subtract this time, because the distance did not change.
That is the whole budget for a body, and it is enough to bury the direct path: 80 cm of
excess range is 2.67 ns of excess delay.

**On what that delay is, and this is a better explanation than the "room echo" this Result
first offered.** An external review pointed out that at channel 9's ~8 GHz, through-torso
propagation is absorbed rather than attenuated, so what the leading-edge detector finds
when blocked is a body-local detour -- diffraction around the torso -- plus a later
threshold crossing on an edge ~12 dB weaker. **Both of those live at the body rather than
in the reader-to-phone geometry, which is exactly why the excess does not scale with
distance** (Result 19). A room reflection would have to depend on where in the room the
phone stands, and Result 19 measured that it does not. The corollary is the uncomfortable
half: a body-local constant should be roughly install-independent but
person-, clothing- and placement-dependent, and every blocked reception here is one torso
in one position. Unverified mechanism, better fit to the data than the one it replaces.

**The shipped classifier separates the two conditions cleanly here**: 4.7% of clear
receptions called obstructed against 81.0% of blocked ones, against 21.7% and 89.2% on
Result 17's messier capture. It remains a sanity check rather than an accuracy figure --
this data is both its test set and, through Results 8-11, its training distribution.

**What it decides.** `unlock_cm` is 100. An 80 cm inflation means a person standing in
their own doorway with the phone on the far side of their body is reported at 155 cm and
never crosses the threshold. The correction the classifier should drive is now a measured
quantity rather than a hypothesis, and it points the safe way: obstruction only ever
inflates range, so undoing it cannot manufacture proximity the radio never saw.

**Still one distance.** Whether 80 cm is a constant or scales with range is unmeasured,
and a constant offset and a scale factor are different code. A second tripod pair at
200 cm settles it and costs four minutes. Everything else here is one geometry, one
subject, one phone, one room, one session.

`ai/tinyml/captures-tripod-1m-2026-08-07.csv` is the data and `ai/tinyml/range_bias.py`
reproduces it. The script's procedure now specifies the tripod, since the first version of
it produced the confound Result 17 had to correct.

## Result 19: the obstruction offset is a constant, and the reader is 25 cm short

Result 18 left one question: is the 80 cm a constant or does it scale with range? Those are
different pieces of code. A second tripod pair at 200 cm, same phone, same room, same
procedure, answers it and hands over a second constant nobody was looking for.

| tape | clear median | offset vs tape | blocked median | bias | n |
|---|---|---|---|---|---|
| 100 cm | 75 cm | **-25.0 cm** | 155 cm | **+80.0** | 107 / 63 |
| 200 cm | 174 cm | **-26.0 cm** | 263 cm | **+89.0** | 155 / 74 |

**The bias is constant.** `bias(200) - bias(100) = +8.0 cm, 95% CI [-9.0, +17.0]`,
bootstrapped over medians, so zero is comfortably inside. The scaling hypothesis predicts
about 160 cm at 200 cm and the observed interval is [73.0, 92.0]: **rejected outright**, not
merely unsupported. A constant offset is what the firmware should carry.

**And the reader under-reports by a constant 25 cm.** That fell out of the same runs and is
not what they were for. Two independent unobstructed measurements, at 100 cm and 200 cm,
give -25.0 and -26.0 cm: agreement to a centimetre across a doubling of range, which is the
signature of a fixed offset rather than a scale error. **This project has never calibrated
the DW3000 antenna delay** and has never had a figure for what that costs. It is 25.5 cm,
or 54 DTU at 4.692 mm per DTU.

**Two constants describe every one of the four runs**, `reported = true - 25.5 +
84.5*obstructed`:

| run | predicted | measured |
|---|---|---|
| 100 cm clear | 74.5 | 75 |
| 100 cm blocked | 159.0 | 155 |
| 200 cm clear | 174.5 | 174 |
| 200 cm blocked | 259.0 | 263 |

Worst error 4 cm across a 2:1 range span and both channel conditions.

**What this means for the lock as it stands today, before any classifier.** `unlock_cm` is
100, and the reader is 25 cm short, so an unobstructed phone triggers the unlock when it is
really at **125 cm**. The door is more eager than its own configuration says. Obstructed,
the same threshold needs a true **41 cm** -- the owner has to be almost touching the door
before the lock will open, which is the failure this whole line of work started from, now
with both halves of it quantified.

**Caveats.** One room, one phone, one subject, one session, and a body is not a door. The
25.5 cm offset is this board and this antenna, not a family constant, and antenna delay is
temperature dependent in ways nothing here measured. The blocked IQRs are 38 and 47.5 cm
against 7 and 11.5 for clear, so a per-reception correction is far noisier than these
medians; anything shipped should act on a filtered range, not a single reception.

`ai/tinyml/captures-tripod-2m-2026-08-07.csv` joins the 1 m set, and
`ai/tinyml/range_bias.py <csv> 200` reproduces the second row.

## Result 20: the shipped feature vector fights the bias it was measured against

Result 19 gave the classifier a concrete job: decide whether to apply a fixed 84.5 cm
correction. That is a binary call on a channel with an 11.5 dB gap, so the question is
whether a model earns its bytes for it at all. Single thresholds, fitted on the 399
tripod receptions pooled across both distances:

| single threshold on | balanced accuracy |
|---|---|
| **rx_pwr** | **0.9136** |
| fp_pwr | 0.9057 |
| fp_resid | **0.6796** |
| pwr_diff | 0.6708 |

One comparison on raw received power beats the shipped tree's honest whole-capture figure
of 0.7729 from Result 11. **These thresholds are fitted in sample and are therefore
optimistic**, so 0.9136 is not comparable to 0.7729 and is not offered as one. The finding
is the ORDER, and the order has a mechanism.

**`fp_resid` is the shipped model's best feature and it comes second from last here.** The
reason is arithmetic rather than statistical, and it does not depend on the accuracy
estimate above.

`fp_resid = fp_pwr + 20*log10(d)`, and `d` is the range **as the reader reports it**, which
Result 19 measured as inflated by 84.5 cm exactly when the channel is obstructed. So the
correction adds power back to precisely the receptions it should be separating:

| | fp_pwr | reported d | fp_resid |
|---|---|---|---|
| clear, 1 m | -70.5 dB | 75 cm | **-73.0** |
| blocked, 1 m | -82.0 dB | 155 cm | **-78.2** |

An 11.5 dB gap in raw power becomes **5.2 dB**, so the feature discards 55% of its own
signal. At 2 m the same arithmetic leaves 7.9 dB of 11.5.

**Why this was invisible until tonight.** Result 11 measured `fp_resid` worth +13.5 points
on captures with a WALKING subject, where genuine distance variation spanned 0.6 to 5.8 m and
dwarfed an 84.5 cm bias. Removing spreading was the dominant effect and the
self-cancellation rode along underneath it. Fix the distance and only the
self-cancellation is left.

**So the feature vector is mis-specified for this job, and not necessarily for that one.**
A model that must work across a real approach still has to handle genuine distance
variation, so `fp_resid` is not simply wrong. What is wrong is feeding it a range the
obstruction corrupts. Two repairs suggest themselves and neither is measured yet: compute
`fp_resid` from a range already corrected by `woz_ml_los_range_true_cm()`, which is
circular unless the class comes from elsewhere; or carry raw `fp_pwr` alongside it and let
the model see both.

**What this does to the case for a model at all.** The calibration constants of Result 19
are what change a lock's behaviour; the classifier is the switch that selects between
them, and a switch with an 11.5 dB gap to work with is not a hard problem. The pieces
worth their bytes are the range correction (34 B) and the confidence margin (56 B, the only
measured way to gate the correction). The depth-2 tree is on notice: it is not beaten yet
on held-out data, but its strongest feature is compromised in the regime that matters.

**Superseded in part by Result 21**: the range correction does not survive a second body,
so the 34 B it costs is no longer a piece worth its bytes. The confidence margin and the
tree are unaffected.

## Result 21: the obstruction is repeatable and its range offset is not

Result 19's two constants were measured in one session, by one person, in one set of
clothes. This is the replication: a second body, different clothing, same room, same
tripod, same 100 cm tape, on 2026-08-07 evening. `captures-tripod3-1m-2026-08-07.csv`,
with the first discarded attempt kept beside it as
`captures-tripod2-1m-2026-08-07-discarded.csv` so the figures below can be re-derived.
Raw `.log` captures are not tracked, per the rule the other sessions follow.

**The channel effect reproduced almost exactly.**

| | clear `fp_pwr` | blocked `fp_pwr` | attenuation |
|---|---|---|---|
| Session 1 (Result 18) | −70.2 dB | −80.3 dB | **−10.1 dB** |
| Session 3 | −73.9 dB | −83.5 dB | **−9.6 dB** |

Half a decibel apart across two bodies. Whatever else is uncertain, **a torso in the first
Fresnel zone costs about 10 dB of first-path power**, and that is the quantity the
classifier keys on. This is the strongest transfer evidence the project has.

Note what did *not* move: `pwr_diff`, Decawave's own NLOS indicator, changed by 3.8 dB and
1.7 dB in the two sessions against `fp_pwr`'s 10. The vendor rule is reading the weak
channel. Result 4 said 76% of the model's importance was absolute power; this is that
finding showing up in a physical measurement rather than a feature ranking.

**The range offset did not reproduce.** Frame length held constant, because
`parse_alab.py`'s mix warning fires on both sessions:

| slice | Session 1 | Session 3 |
|---|---|---|
| all frames | +80.0 cm [+72, +92] | **+115.0 cm** [+84, +134] |
| `len=46` only | +82.0 cm [+62, +93] | **+127.0 cm** [+109, +136] |

The `len=46` intervals do not overlap. +80 and +127 are not estimates of one constant.

**The likeliest mechanism, and it is inference rather than measurement.** Session 1's
subject stood midway between phone and reader; session 3's stood with their back nearly
against the tripod, because the capture instructions were tightened between runs. A nearer
blocker occludes more of the Fresnel zone and forces a longer detour, so the offset plausibly
tracks *where* the body is and not merely *whether* it is there. Session 1's own 1 m/2 m
pair could not have seen this: it varied reader-to-phone distance while keeping the subject
midway, so blocker position was held fixed by accident.

**Consequence: `range_correct_en` stays off permanently and its obstruction half should be
considered unshippable.** A constant that swings 47 cm between two honest measurements will
subtract the wrong amount at a real door, and the direction that hurts is the one that opens
a lock for someone who is 185 cm away. The unconditional antenna half is untouched by this:
it measured −25.0, −26.0, −21.0 and −24.0 cm across four runs and two sessions, which is
still an offset.

**What to build instead.** The decision the classifier can support is a threshold widening,
not a subtraction: *if obstructed with confidence, raise `unlock_cm` for the duration*.
That needs the sign of the effect, which replicated, and not its magnitude, which did not.
It also fails safe — an unobstructed phone keeps the unchanged gate — and it is what the
project actually wants, since a pocketed owner should gain permission rather than have a
number quietly edited underneath them.

**Two runs were discarded to get this one**, and both failures are the reason the procedure
in `range_bias.py` is written the way it is. The first blocked run showed −2.0 cm frame
matched: only 5 of 39 receptions had a channel signature different from clear, so the body
was in the path for a fraction of the capture. The second attempt moved the tripod between
runs, which destroys the cancellation the whole design rests on. **The cheap guard is to
watch `d=` during the blocked run**: it must sit near tape+80, and if it reads near the tape
figure the subject is not blocking. Ten seconds of watching would have caught both.

## Next

1. ~~Runtime code size on target.~~ **Done: Result 6**, then re-measured on every change
   since: **776 B** for everything the module now contains, extractor included
   (Result 12). It was 466 B before the extractor (Result 11), 300 B on the depth-3
   four-feature build, 308 B on the four-feature eWINE one and 390 B on the original
   14-feature one; those figures survive below and in Result 6 as the record of how
   each build measured.
2. ~~The tree behind a `woz_ml` seam with sklearn-certified golden vectors in CI.~~
   **Done: `modules/woz_ml/`.** `ai/tinyml/gen_model.py` generates the tree, the scaler
   and 82 leaf-stratified golden vectors, behind four gates: pruning must change no
   prediction on the full set, the generated C must match sklearn on every held-out
   sample, the float32 scaler must match the float64 one the model was trained through,
   and the libm-free range correction must classify identically to `numpy.log10`.
   `tests/host/test_woz_ml.c` runs on every `make test`. Nothing calls the classifier:
   `CONFIG_WOZ_ML_LOS` is default n and no image sets it.

   One finding from building it. **The fitted tree is 31 nodes and 16 leaves, of which
   only 13 nodes and 7 leaves decide anything**: more than half the splits separate
   samples without changing the class, because impurity still falls. The generator now
   collapses them. It buys no flash, since the compiler was already folding the identical
   branches and the tree measures 76 B either way, but the generated C now says what the
   model decides rather than what it was fitted with, and clang-tidy's
   `bugprone-branch-clone` stops objecting. Worth knowing before reading a tree diagram
   as if every split mattered.
3. ~~**Real door captures — scalars only, per Result 7.**~~ **Done: Results 8 and 9.**
   `make cirdiag` builds an image with an unattended arm/drain cycle, because the
   DWM3001CDK has no console input and the upstream `aliro cir dump on` workflow is
   unavailable on it. Capture the `[ALAB] ev=uwb.diag` summary and nothing else:
   `ipatovPower`, `ipatovF1..F3` and `ipatovAccumCount` are the whole feature set.
   556 receptions are in hand and the shipped thresholds are fitted on them.
4. ~~**A second clear capture.**~~ **Done, and it changed the story.** Both sides now
   survive holding out a whole capture session, but the two unobstructed sessions differ
   by 2.9 dB in mean `fp_pwr`, a third of the obstruction signal, with nothing changed
   between them but the hour. Session drift, not geometry, is the largest source of
   variation measured so far, and Result 11 is what buys most of it back.
5. **The on-target feature extractor**, now down to its last piece.
   `woz_ml_los_classify()` takes two floats; `woz_ml_los_fp_resid()` supplies the awkward
   one, and `rx_pwr` is a handful of arithmetic on the same `dwt_rxdiag_t`.
   `ai/tinyml/parse_alab.py` is the definition it has to reproduce register for register.
6. **Wire it to a decision**, which has to answer what a wrong answer costs at a door.
   Deliberately not done. `woz_ml_los_disagrees()` exists so the vendor rule can run as
   shadow telemetry first: the tree-vs-vendor disagreement rate is the cheapest drift
   monitor available and needs no labels. Nothing counts it yet.

   **What this classifier is, stated plainly, because the obvious reading of it is
   wrong.** `clear` is the phone in hand facing the reader and `obstructed` is the phone
   behind the back or in a back pocket (Result 13). Both classes are a body and there is
   no door in any of the training data. So this is a **carry-position detector**, and a
   pocketed phone is the ordinary walk-up rather than the suspicious one. Gating an
   unlock on `obstructed` would refuse the most common legitimate entry there is.

   **So the rule is: let it compensate a measurement, never let it adjudicate intent.**
   A weaker first path is detected later, so an obstructed reception should read farther
   than it is, and an owner with a pocketed phone unlocks late or not at all. Correcting
   that ADDS permission, and it is safe to add because it does not touch the check that
   defends against a distance-reduction attack: STS quality, layer 2 in
   `docs/range-integrity.md`, which a spoofed early first path cannot reproduce.
   Distance-enlargement is not an attack, and `docs/range-integrity.md` already records
   the asymmetry this rests on: "Presence fails closed; the lock does not."

   **The sign and size of that bias are unmeasured on this board**, and everything above
   is a hypothesis until they are not. `ai/tinyml/range_bias.py` is the experiment, and
   it needs no firmware change: `make cirdiag` already captures the registers and the
   range. Two captures at one tape-measured spot, standing still, phone in hand and then
   in a back pocket. If the bias is near zero there is nothing to compensate and this
   line of work stops; if it is negative, a pocketed phone reads CLOSER than it is and
   that is a security finding rather than a usability one.

   **Standing still is not optional.** Result 13 could not answer its own question because
   the subject walked, and a bias at a known distance is exactly the quantity that motion
   destroys.

   ~~Then an extractor, then retrain.~~ **Done: Result 8.** `ai/tinyml/parse_alab.py`
   turns a capture log into the four features; 309 samples off this board give
   0.7958 ± 0.0532 against a 0.7074 refit-vendor-rule baseline.

   Flash with `make flash` and never `flash-erase` — the walk-up needs the Apple Home
   credential, and do not touch SW2 either, since held through a reset it is the factory
   reset. One condition per run, one file per run, and **stay blocked for the whole
   blocked run**: see Result 8 for what the exposed phase costs.

   Still open: more samples (97 blocked is thin), and a second geometry to say anything
   about generalisation. The eWINE tree remains substrate, not a pretrained model to
   fine-tune — at 158 B the model is cheaper to retrain than to transfer.
4. **Keep the vendor rule in shadow telemetry.** `pwr_diff > 6 dB` reuses the same 112 B
   scaler and adds one comparison, and it is the floor the tree has to keep beating.
   Logging tree-vs-vendor disagreement rates is the cheapest drift monitor available: no
   labels needed, and a rate that moves means either the install changed or the model went
   stale.
5. Revisit TFLM only if a future capability (the CIR autoencoder for relay detection)
   actually needs a neural network.
