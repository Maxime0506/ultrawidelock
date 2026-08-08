# Embedded ML (TinyML) for OpenAliro: Framework, Capabilities, and Pipeline

Research deliverable, landed 2026-08-06 from the `worktree-tensorflow` worktree. Nothing
here is built or measured on this repo's hardware yet; every figure below comes from
external literature or vendor material and is feasibility evidence, not a guarantee.

**Correction against measured numbers, and it loosens the whole budget.** The Caveats
section as originally drafted said the DWM3001CDK sits at "~97% RAM utilization", and
`CLAUDE.md` / `firmware/README.md` both quoted 6,060 B of free RAM. All of that was stale,
and the reason was chased down on 2026-08-06 (see commit c6c3c61a). Budget against the
**shipping** image, `SMP=1 RELEASE=1` with LTO on, which is what `make release` builds and
`make fota` pushes, and which is the only configuration carrying a maintained measurement:

| image | flash used | of | RAM used | of | source |
|---|---|---|---|---|---|
| shipping (`SMP=1 RELEASE=1`) | 379,332 B | 433,664 B (87.47%) | 111,012 B | 131,072 B (84.70%) | `firmware/size-baseline.json` primary, CI-measured at `a8979990` |

So the headroom that matters is **20,060 B of RAM and 54,332 B of flash**. The 6,060 B
figure was a *debug* `make build` measured before 73237fb8 (#8), which dropped the Zephyr
IP layer and took `CONFIG_LOG_DEFAULT_LEVEL` from 3 to 1, worth 37,956 B of flash and
10,224 B of RAM on the shipping image; f8fdcf4f (#11) re-recorded the baseline and no prose
followed. The `cdk-size` workflow was comparing shipping against shipping with current data
the whole time and was never masking anything, and `make verify`'s `cdk-size` gate is
`tests/tooling/cdk_size_test.sh`, which pins the accounting rule against fixtures and never
reads the baseline's numbers at all.

Do not quote the debug `make build` image without rebuilding it: it has no current clean
measurement. A figure of 16,540 B RAM / 40,156 B flash appeared in an earlier draft of this
file; it was a debug build carrying unmerged LED work from another worktree, and it was
measuring the wrong image besides.

Consequence for this plan: TFLM's ~4 KB `AllocateTensors()` stack plus a single-digit-KB
arena comfortably fits the shipping image, where against 6,060 B it did not. The
head-to-head against emlearn was therefore run on merit rather than on desperation, and
emlearn won anyway. See `ai/tinyml/RESULTS.md`.

---

## TL;DR
- **Ship TensorFlow Lite Micro / LiteRT for Microcontrollers (int8) with ARM CMSIS-NN kernels as the primary runtime.** It is the de-facto MCU standard, is a first-class Zephyr module (`CONFIG_TENSORFLOW_LITE_MICRO`), builds unmodified for x86 CI, and its CMSIS-NN int8 kernels are bit-exact with the TFLM reference kernels, giving provable golden vectors. But for the v1 tabular problem (10-20 UWB diagnostic features), a random-forest / gradient-boosted-tree via **emlearn** will likely beat a quantized MLP on accuracy-per-byte and must be benchmarked head-to-head first.
- **The single highest-value capability** is inside-vs-outside / through-door discrimination plus LOS/NLOS classification from UWB channel diagnostics, a well-studied problem reaching ~90% accuracy from CIR features downsampled 20x (Barral et al., Springer 2024) and up to 93.7% with a two-stage SVM on DWM1000 CIR features. Next after that: relay / distance-fraud PHY anomaly detection (>99% detection via a channel-reciprocity autoencoder validated on the DW3110, Gou et al. 2024), then accelerometer knock / forced-entry sensing. All three are achievable in a few KB.
- **On the DWM3001CDK (nRF52833, 128 KB RAM, 20,060 B free in the shipping image) the TFLM interpreter looks affordable.** Interpreter core is ~2 KB, a small MLP arena is single-digit KB, and the ~4 KB stack for `AllocateTensors()` plus the CMSIS-NN kernel flash (against 54,332 B free) are the real budget items. Superseded by measurement: it fits, and it still lost to a 238 B tree on accuracy per byte. See `ai/tinyml/RESULTS.md`.

## Key findings

1. **TFLM is alive and is the right default.** TensorFlow Lite Micro was rebranded "LiteRT for Microcontrollers" in 2024 but remains the same codebase in the `tensorflow/tflite-micro` repo, still actively released (nightly PyPI builds dated into February 2026). It is a first-class Zephyr upstream module with `hello_world` and `magic_wand` samples that build for both `qemu_x86` (host CI) and Cortex-M with CMSIS-NN. Nordic ships it in NCS.

2. **The classical-ML route is a serious contender for v1.** The v1 problem is tabular classification on 10-20 hand-engineered UWB features, exactly where decision-tree ensembles shine. emlearn (Jon Nordby) compiles scikit-learn / Keras models to portable C99 with no dynamic allocation, from ~2 KB flash and under 100 bytes RAM, requires no libc, and ships as a Zephyr module. Per LWN.net ("Milliwatt machine learning with emlearn", Feb 2025) it is referenced in at least 40 scientific publications, and a deployed cattle-activity decision tree consumed under 1 mW, ~50x less power than streaming raw data. UWB NLOS work routinely uses SVM / decision-tree / random-forest on CIR-derived features.

3. **Edge Impulse is a powerful but partly proprietary alternative.** Its EON Compiler is interpreter-less codegen over the same LiteRT flatbuffer and reduces RAM ~25-55% and flash up to ~35% vs vanilla TFLM (enterprise messaging claims 70% RAM / 40% ROM). Nordic blesses it via an official NCS wrapper. The free Developer Plan now includes production licensing. Downsides for an open-source project: custom UWB binary logs are not a native ingestion format, and the RAM-Optimized EON mode is enterprise-gated.

4. **Capabilities are real and quantified in the literature** (details below): LOS/NLOS ~90% (93.7% with SVM) from CIR features; inside/outside is patented access-control practice (AoA + RSSI + SNR cross-checks); relay / Ghost-Peak anomaly detection >99% via a CIR-reciprocity autoencoder; occupancy / people-counting up to 99%; activity recognition ~95%.

5. **Bit-exact golden vectors are achievable** if, and only if, the host oracle uses the TFLM reference kernels, not full TensorFlow Lite.

## Details

### Part A: framework / runtime selection

**A.1 TensorFlow Lite Micro / LiteRT for Microcontrollers, current state.**
The runtime formerly called TFLM was rebranded "LiteRT for Microcontrollers" by Google in 2024; the microcontroller code continues to live in the separate `tensorflow/tflite-micro` GitHub repo and is still built and published (pre-release wheels on PyPI dated into 2026), so it is maintained, not abandoned. It is interpreter-based over a FlatBuffer model, uses a single statically-provisioned tensor arena with no runtime malloc, and includes only the kernels you register.

Footprint facts:
- **Interpreter core ~2 KB.** The statically-compiled interpreter core is about 2 KB (David et al., "TensorFlow Lite Micro", arXiv 2010.08678); at build time only the kernels actually used are linked via a minimal OpResolver.
- **Tensor arena for a small MLP is single-digit KB**, and must be tuned empirically. TFLM's own guidance is that arena size "will depend on the model you are using, and may need to be determined by experimentation". The classic `hello_world` sine MLP uses roughly a 2 KB arena.
- **`MicroMutableOpResolver<N>` shrinks flash** by registering only needed ops (e.g. `micro_op_resolver.AddFullyConnected()`), versus `AllOpsResolver` which pulls in all kernels. Silicon Labs' from-scratch guide confirms a fully-connected-only model needs just `MicroMutableOpResolver<1>` with `AddFullyConnected()`. This is the single most important flash-savings lever for a small model.
- **~4 KB of stack** must be reserved for `AllocateTensors()` / `Invoke()`, and the arena should be 16-byte aligned.
- **CMSIS-NN gives ~2.4x inference speedup on Cortex-M4** versus reference C kernels (3.2x on M7); ARM has reported up to 6-7x for individual conv / fully-connected layers. CMSIS-NN is bit-compatible: it follows the int8/int16 quantization spec of TFLM.

**A.2 Alternatives evaluated for Cortex-M4F / 128 KB class.**

- **Edge Impulse (EON compiler):** interpreter-less codegen; ~25-55% RAM and up to ~35% flash reduction vs TFLM. Independent EdgeMark benchmarking (arXiv 2502.01700) found flash savings ranging from negligible to over 100% depending on model, slightly better numerical accuracy than TFLM, while vanilla TFLM was sometimes faster on small FC models. First-class NCS wrapper. Free Developer Plan now includes production licensing. Concern for OpenAliro: custom UWB binary traces are not a native sensor type (need the BYOM / CSV path), and the strongest RAM-Optimized EON mode is enterprise-only. Good as an exploration / EON-export tool, weaker as the canonical open-source pipeline.
- **Direct CMSIS-NN hand-roll:** smallest possible footprint for a 20-input MLP (you call `arm_fully_connected_s8` yourself); highest maintenance cost, no converter, but perfectly deterministic and bit-exact. Reasonable as a final-mile optimization on the CDK if TFLM overhead is intolerable.
- **microTVM / Apache TVM AOT:** compiles to ahead-of-time C, can emit CMSIS-NN, supports Zephyr and lists the nRF5340 DK as a supported board, but it remains "under heavy development", TVMC does not cleanly support micro targets, and the toolchain complexity is high. Not worth it in 2026 for a first embedded-ML project of this size.
- **emlearn:** scikit-learn / Keras to portable C99; from ~2 KB flash, <100 B RAM; no libc; Zephyr module; supports random forests, decision trees and small MLPs, with an `inline` codegen mode that emits dependency-free if/else trees. Ideal fit for feature-vector classification. Multiple real deployments on nRF52 / Contiki-class MCUs.
- **Codegen tools (m2cgen, emlearn, tinymlgen, ONNX2C) and Neuton.ai:** m2cgen and emlearn are the practical open-source options; Neuton.ai claims models "as small as 5 KB" and up to 1000x smaller than TF but is a proprietary no-code cloud tool (has a Nordic partnership). For an open-source project prefer emlearn / m2cgen for classical models.
- **MCUNet / TinyEngine (MIT):** aimed at ImageNet-class CNNs on MCUs; overkill and irrelevant for 10-20-feature tabular inference.

**A.3 Verdict.**
- **Roomy dev phase (nRF5340 DK / ESP32-S3):** TFLM (LiteRT-for-MCU) int8 + CMSIS-NN, as the Zephyr module. Train in Keras, convert to a `.tflite` flatbuffer.
- **Ultra-constrained production build (nRF52833 CDK):** keep the same `.tflite` artifact if it fits; if the full-build RAM / flash budget is too tight, fall back to (a) `MicroMutableOpResolver` trimming plus arena minimization, then (b) emlearn-generated C for a tree / MLP as the smallest-footprint escape hatch.
- **Migration story:** identical Keras training and identical int8 quantization; the same flatbuffer runs on the x86 CI oracle (TFLM reference kernels) and the Cortex-M target (CMSIS-NN), which are bit-exact.
- **Classical-ML alternative:** for v1 tabular features, benchmark an emlearn random forest / gradient-boosted trees against the int8 MLP; on accuracy-per-byte, trees frequently win for low-dimensional tabular data.

### Part B: capabilities

| Capability | Data needed | Reported accuracy (literature) | Few-KB feasibility |
|---|---|---|---|
| LOS/NLOS classification | CIR features or raw CIR | ~90% (CIR downsampled 20x, Barral/Springer 2024); 93.7% two-stage SVM on DWM1000; ResNet 96.5% on eWINE | High (feature MLP/tree) |
| Inside/outside (through-door) | AoA/PDoA, RSSI, SNR, first-path | Patented access-control practice (AoA + RSSI + SNR cross-checks); productized | High |
| Relay / distance-fraud PHY anomaly | CIR consistency/reciprocity | >99% detection (reciprocity autoencoder on DW3110, Gou et al. 2024) | Medium (tiny autoencoder/tree) |
| Occupancy / people counting | CIR (LOS) | up to 99% (ensemble); 81-86% single CIR | Medium |
| Human activity recognition | CIR time-series | ~95% (stand/sit/lie) | Medium |
| Approach-intent / trajectory | distance/velocity/accel time-series | industry practice ("walking toward vs past") | High (GRU/TCN or tree) |
| Knock / forced-entry / door-state | LIS2DH12 accelerometer | patented; knock ML documented | High |
| Gait / person ID | CIR/ranging time-series | realistic as verification/anomaly, not robust identity | Low-Medium (honest) |

Key notes:
- **LOS/NLOS is the flagship, best-documented capability.** Feature sets are stable across the literature: first-path power vs total RX power ratio, RXPACC, first-path index, kurtosis, RMS delay spread, mean excess delay, skewness. Qorvo's own APS006 Part 3 ("DW1000 Metrics for NLOS Channels") defines device-provided LOS/NLOS metrics (e.g. the saturation metric Mc>0.9 implying a LOS path), so this extends vendor-sanctioned signal processing rather than inventing it.
- **The relay / fraud threat is concrete and current.** The Ghost Peak attack (Leu et al., USENIX Security 2022, arXiv 2111.05313) reduced measured HRP-UWB distance from 12 m to 0 m against Apple U1 / NXP / Qorvo chips with success probabilities up to 4% using only a ~USD 65 off-the-shelf device. The CIR-reciprocity autoencoder defense (Gou et al., arXiv 2405.18255) detects it at >99% and is explicitly validated on the DW3110, the same radio family this project uses, making it a strong, board-relevant advisory feature.
- **Environment-specificity is the main caveat.** ML NLOS models degrade in unseen environments; transfer learning recovers ~15% classification accuracy / 50% error. This argues strongly for an on-device auto-label flywheel: per-install fine-tuning.
- **Public pre-training data exists.** The eWINE `UWB-LOS-NLOS-Data-Set` (Bregar & Mohorcic, 2018; DW1000, 1016-sample CIR) contains, per the project README, "3000 LOS samples and 3000 NLOS samples" in each of 7 indoor locations, "42000 samples ... 21000 for LOS and 21000 for NLOS", deliberately spanning environments "to prevent building of location-specific models". Directly usable for pre-training before device-specific traces are collected.
- **Gait / person ID: be honest.** Robust identity from UWB alone is not realistic on a few-KB model; frame it as anomaly detection / weak verification layered on the credential, never as a primary authenticator.
- **No Qorvo ML app note and no published TinyML-on-DWM3001CDK prior art exist.** Capability precedent is on DWM1000 / MDEK1001-class hardware and mostly off-device, so an on-device model on the DWM3001CDK is genuinely novel.

### Part C: setup / tooling / training pipeline

**C.1 End-to-end workflow.**
1. **Data collection:** replay the on-device flight-recorder binary traces host-side; parse versioned records into feature rows.
2. **Labeling (the flywheel):** every credentialed unlock attributes a Matter user, giving free supervised labels (user identity, plus implicit inside/outside from unlock success).
3. **Training env:** Python 3 plus TensorFlow / Keras for MLP / CNN; scikit-learn for trees (to emlearn). Pin a TF version whose `tf.lite` converter matches the TFLM commit's flatbuffer schema.
4. **Quantization:** full-integer int8 post-training quantization with a representative dataset (`converter.optimizations=[tf.lite.Optimize.DEFAULT]`, `representative_dataset=...`, `target_spec.supported_ops=[TFLITE_BUILTINS_INT8]`, `inference_input/output_type=tf.int8`). Use quantization-aware training only if PTQ drops accuracy unacceptably, which is rare for tiny MLPs.
5. **Conversion:** `.tflite` flatbuffer; register ops with `MicroMutableOpResolver`. Common failure mode: an unsupported / unfused op, or the converter leaving float in/out, which forces extra de/quant layers.
6. **Arena sizing:** start high (tens of KB), call `AllocateTensors()`, read `arena_used_bytes()`, then shrink to the measured value plus margin.
7. **Zephyr / NCS integration:** enable `CONFIG_CPP=y`, `CONFIG_REQUIRES_FULL_LIBC=y`, `CONFIG_TENSORFLOW_LITE_MICRO=y`; pull the `tflite-micro` module via West (`west config manifest.project-filter -- +tflite-micro; west update`). Run inference in a low-priority thread on features popped from the post-ranging ring buffer, never on the DS-TWR timing path.
8. **Latency / power:** measure on-device; small int8 models on Cortex-M4 run in low single-digit to tens of milliseconds; budget accordingly for a low-priority background thread.

**C.2 Edge Impulse as an alternative full pipeline.** Good for rapid experimentation and EON export; its data forwarder and DSP blocks assume standard sensor types (audio / IMU), so custom UWB binary logs need the BYOM / CSV path rather than native ingestion. Free Developer Plan is adequate for an open-source prototype and now permits production deployment.

**C.3 Host-side testing and golden vectors.** Build TFLM for x86 in CI (the `qemu_x86` / host reference-kernel build). **Critical:** per ARM's CMSIS-NN README, int8/int16 CMSIS-NN "is bit-exact with Tensorflow Lite reference kernels. In some cases TFL and TFLM reference kernels may not be bit-exact. In that case CMSIS-NN follows TFLM reference kernels." Practical consequence: the host oracle must use the **TFLM reference kernels**, not full TensorFlow Lite. Do that and host and target produce identical int8 logits, making "model in, expected logits out" golden vectors reliable across x86 and Cortex-M.

**C.4 Model-as-blob deployment.** Ship the `.tflite` flatbuffer as a swappable blob over the existing signed MCUboot DFU path (P-256). Add a small versioned header (schema version, model ID, input/output quantization params, feature-set version) so firmware validates compatibility before loading. Watch flatbuffer schema compatibility across TFLM versions: pin the runtime commit and the converter together.

**C.5 MLOps for tiny fleets.** Lightweight, open-source-friendly stack: Git plus DVC (or plain versioned trace archives) for dataset versioning; MLflow for experiment tracking; shadow-mode telemetry (log model outputs vs ground-truth unlocks) for drift monitoring. Ship every feature shadow, then advisory, then enforce (opt-in).

**C.6 Learning path for an embedded expert new to ML.**
- **Learn first:** supervised classification basics; the int8 quantization mental model (scale / zero-point); the TFLite converter; arena sizing.
- **Skip for now:** deep CNN architecture theory, GPU training pipelines, transformers.
- **Common beginner failure modes:** (1) train/serve feature skew, where the embedded feature extraction must byte-match the training pipeline; (2) oversized arena starving I/O buffers; (3) using `AllOpsResolver` and blowing the flash budget; (4) forgetting int8 in/out and shipping float de/quant layers; (5) non-deterministic latency from doing ML work in an ISR.

## Recommendations

**Primary stack:** TFLM / LiteRT-for-MCU int8 plus CMSIS-NN as a Zephyr module, Keras training, `.tflite`-over-DFU, TFLM-reference-kernel host oracle for golden vectors. **Run an emlearn random-forest / GBT baseline in parallel for v1** and ship whichever wins accuracy-per-byte on the CDK budget.

**Concrete first-two-weeks plan (all on the nRF5340 DK):**
- **Days 1-2:** Build and flash the Zephyr `tflite-micro` `hello_world` and `magic_wand` samples on the nRF5340 DK; build the same for `qemu_x86` to establish the host CI path.
- **Days 3-4:** Stand up the training env; pull the eWINE UWB LOS/NLOS dataset; train a baseline LOS/NLOS MLP and an emlearn random forest on CIR-derived features (first-path/total-RX ratio, RXPACC, first-path index, kurtosis, RMS delay spread).
- **Days 5-6:** int8 PTQ the MLP; generate golden vectors host-side with TFLM reference kernels; confirm bit-exact logits on the nRF5340 DK vs x86.
- **Days 7-8:** Wire the flight-recorder replay to feature CSV to training loop; validate auto-labeling from Matter attribution.
- **Days 9-10:** Integrate inference in a low-priority Zephyr thread reading the post-ranging ring buffer; measure arena (`arena_used_bytes()`), latency, and flash/RAM delta.
- **Days 11-14:** Port to the DWM3001CDK; measure real RAM headroom in the reader-only build first; decide TFLM vs emlearn for the full build based on measured budget; ship the winner in shadow mode.

**Benchmarks that change the plan:** if the full-build TFLM RAM delta cannot be squeezed under the available budget after `MicroMutableOpResolver` trimming and arena minimization, switch that build to emlearn-generated C. If PTQ int8 accuracy drops materially vs float, add quantization-aware training. If per-install NLOS accuracy is poor, enable on-device transfer-learning fine-tuning via the flywheel.

## Caveats
- **Headroom on the CDK is 20,060 B of RAM and 54,332 B of flash, not 6,060 B, and flash is the binding region on the image that ships.** See the correction at the top of this file for the cause. `firmware/size-baseline.json`'s primary entry is the authority and is current; the prose that quoted 6,060 B was fixed in c6c3c61a. Budget against a byte figure from that file or from a fresh linker region report, never a percentage and never the README.
- **Exact DW3000 CIR read size needs confirmation.** The accumulator is 1016 complex samples at 64 MHz PRF; the DW1000 read was 4064 bytes (16-bit I/Q), while DW3000 example code (`ex_02c_rx_diagnostics`) documents 24-bit I/Q (~6 KB for a full read). Confirm the windowed read length in the DW3000 User Manual (Table 41, SPI indexed read of accumulator CIR memory) and `dwt_readaccdata()` in the DW3720 API guide before committing SPI / RAM budget for the v2 CIR path.
- **NLOS / relay ML accuracy figures are environment- and attack-specific** and mostly from off-device research hardware (DWM1000 / MDEK1001-class); treat them as feasibility evidence, not guarantees for the CDK. The one directly board-relevant result is the DW3110-validated reciprocity autoencoder.
- **Vendor claims** (Edge Impulse EON percentages, Neuton "5 KB" / "1000x") are marketing; validate on the actual model before relying on them.
- **Gait / person-ID as a primary authenticator is not realistic** on this hardware; use it only as an advisory anomaly signal layered on the credential.

## Status log

- 2026-08-06: research landed. Decision taken: start with the **host-only bake-off**
  (eWINE LOS/NLOS dataset, int8 MLP vs emlearn random forest, accuracy-per-byte table).
  No NCS toolchain, no `make ws-seed`, no firmware changes until the bake-off picks a
  runtime.
- 2026-08-06: **bake-off done, and it reverses the recommendation above.** Full results in
  `ai/tinyml/RESULTS.md`. An emlearn depth-4 decision tree scores 0.8625 at 238 B of
  payload against 0.8662 at 2,440 B for the best int8 MLP and 0.8684 at 30,802 B for the
  best random forest, so TFLM is not worth its interpreter on this problem. Four things
  the research document did not anticipate:
  1. The DW3000 `dwt_rxdiag_t` has **no noise fields**, so `STDEV_NOISE`/`MAX_NOISE` and
     anything derived from them are DW1000-only and cannot be fed on this hardware
     (~1.5 accuracy points).
  2. `modules/woz_uwb/src/driver/uwb_cirdiag.c` **already captures** the scalars and a
     64-tap CIR window and emits them as `[ALAB]` lines, so the data path exists.
  3. About 76% of the feature importance is absolute received power, and a power-free
     model scores only ~0.74, so the headline ~0.86 is partly a distance artefact of the
     public data set and is an optimistic ceiling for a fixed-geometry door.
  4. emlearn's generated C matches sklearn essentially exactly for single trees but
     diverges on ~1% of samples for forests, so only the single tree can be certified
     against its training-side model.
  The plan's "run an emlearn baseline in parallel" instinct was right; its default choice
  of TFLM was not.
