<!-- generated documentation — edit the source, not this file -->
# `modules/woz_ml/src/woz_ml_feat.c`

@file woz_ml_feat.c — five CIA registers and a range, turned into the model's input.
This is the half of the classifier that was missing: woz_ml_los_classify()
has always taken features in physical units, and until now nothing computed
them. ai/tinyml/parse_alab.py is the definition, and it is reproduced here
register for register rather than approximated, because the model was fitted
through exactly this arithmetic.
num    = F1^2 + F2^2 + F3^2
fp_pwr = 10*log10(num / C^2)          - A
rx_pwr = 10*log10(area * 2^17 / C^2)  - A
where C is ipatovAccumCount and area is ipatovPower, the DW3000's 17-bit
"channel area". The 2^17 undoes that scaling, which is the one step in the
DW3000 formula that differs from the DW1000's and the one most likely to be
dropped by someone porting this from a DW1000 note.
A IS NOT A CALIBRATION AND ITS VALUE DOES NOT MATTER HERE. It is eWINE's
PRF-64 constant, kept only so this arithmetic matches extract_features.py
exactly. It is a constant offset applied to both powers, so it cancels out of
pwr_diff entirely and shifts the tree's thresholds by a fixed amount that the
training already absorbed. Changing it does not recalibrate anything; it
invalidates the model.
ZERO IS A FAILED READ, NOT A WEAK SIGNAL. A zeroed accumulator count or
channel area comes back from a CIA read that did not complete, and this is
not hypothetical: the CPER-set receptions in a real capture report
ipatovPower = 0. Left in, the logarithm turns them into -120 dB outliers that
dominate the mean and inflate the spread by 20 dB. parse_alab.py drops those
receptions and so does this, by returning false.

**depends on** [`modules/woz_ml/include/woz_ml.h`](../modules.woz_ml.include/woz_ml.h.md), [`modules/woz_ml/src/woz_ml_log2.h`](woz_ml_log2.h.md)  ·  **discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

## API

### `static float pwr_db(uint64_t numerator, uint32_t count)`
`modules/woz_ml/src/woz_ml_feat.c:62`

10*log10(numerator / count^2) - A, with the division done as a subtraction of
logarithms so no float division appears and the numerator never has to be
representable as a float in the first place. count is squared in the log
domain for the same reason: count^2 is fine in 32 bits, but keeping it here
means one code path and one rounding behaviour for both callers.

**called by** `woz_ml_los_features`

<details><summary>Undocumented (1)</summary>

- `woz_ml_los_features` — tested: woz ml

</details>
