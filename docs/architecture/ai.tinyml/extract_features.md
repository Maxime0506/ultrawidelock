<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/extract_features.py`

Extract LOS/NLOS features from the eWINE UWB data set.

Run:
    ai/tinyml/.venv/bin/python ai/tinyml/extract_features.py

Options (env vars):
    EWINE_DIR   dataset root (default ai/tinyml/data/UWB-LOS-NLOS-Data-Set-master)
    OUT         output .npz (default ai/tinyml/features.npz)
    WIN_PRE     CIR taps kept before the first-path index (default 15)
    WIN_POST    CIR taps kept after it (default 140)
    PORTABLE    1 = emit only features the DW3000/DW3110 can actually produce

PORTABLE matters. This data set is DW1000, whose diagnostics include STDEV_NOISE and
MAX_NOISE. The DW3000 `dwt_rxdiag_t` in deps/dw3000/dwt_uwb_driver/deca_device_api.h has
no equivalent field, so stdev_noise, max_noise and the derived fp_snr cannot be computed
on this project's hardware. Training with them would produce a model that cannot be fed
on the target: the classic train/serve skew. PORTABLE=1 drops all three.

The firmware already captures CIR: modules/woz_uwb/src/driver/uwb_cirdiag.c reads
CIRDIAG_CIR_WIN = 64 Ipatov taps centred on the first-path index. To match that capture
exactly, use WIN_PRE=32 WIN_POST=32.

Two traps in the upstream data set, handled here:
  * its README normalises the CIR with `item[2]`, but column 2 is FP_IDX; RXPACC is
    column 9. Dividing by FP_IDX is wrong and this script divides by RXPACC.
  * its reference loader `code/uwb_dataset.py` calls `df.as_matrix()`, removed from
    pandas long ago, so it cannot run on a current environment at all.

Deliberately NOT used as features, to keep the model about the channel rather than
about this particular capture rig:
  RANGE       the measurement outcome, and strongly site-specific
  CH/BITRATE/PRFR   constant across the whole set (2 / 110 kbps / 64 MHz): zero information
  FRAME_LEN, PREAM_LEN   protocol/config values that vary with the capture setup, so a
              model can latch onto them as a site fingerprint instead of learning physics

**discussed in** [`ai/README.md`](../../../ai/README.md), [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

<details><summary>Undocumented (3)</summary>

- `load_raw`
- `extract`
- `main`

</details>
