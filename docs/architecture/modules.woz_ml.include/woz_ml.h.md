<!-- generated documentation — edit the source, not this file -->
# `modules/woz_ml/include/woz_ml.h`

@file woz_ml.h — on-device classifiers, and the seam that keeps them replaceable.
One classifier so far: line-of-sight vs obstructed, from the DW3000's own
receive diagnostics. It answers "is there a door between the reader and the
phone", which the ranging distance alone cannot: an obstructed 1 m and a clear
3 m look similar to a time-of-flight estimate and do not mean the same thing.
WHAT THIS IS NOT. It is not wired into the ranging path, and nothing calls it
yet. It is compiled, certified against its training-side model on every build
of the host suite, and sized. Wiring it to a decision is a separate change
that has to answer what a wrong answer costs.
THE MODEL IS A DECISION TREE ON PURPOSE. Two integer comparisons; 776 B of
flash for everything here -- feature extraction, classification, the range
correction and the drift monitor -- 0 B of RAM, 28 B of stack. That is 1.4% of
the shipping image's free flash. No arena, no interpreter, no dynamic
allocation, nothing to
fail at init, and not one undefined symbol -- `arm-none-eabi-nm -u` on the
objects is empty, so the measured size is the whole size. It was measured
against an int8 TFLM network and a random forest and beat both on accuracy per
byte (ai/tinyml/RESULTS.md). It is also the only one of the three whose C can
be proved identical to the model that was trained -- see the header of
ai/tinyml/gen_model.py for the four gates that enforce that on every
regeneration, and tests/host/test_woz_ml.c for the vectors that carry the
claim into CI.
FEED IT THE RIGHT NUMBERS. woz_ml_los_classify() takes raw features in
physical units, in the order enum woz_ml_los_feature declares, and getting
that array wrong cannot be detected -- every float is a legal feature value.
So do not fill it by hand: woz_ml_los_features() below takes the five CIA
registers and the range and fills it, reproducing ai/tinyml/parse_alab.py
register for register, and gate 4 in ai/tinyml/gen_model.py holds it to that
on every captured reception.

**depends on** [`modules/woz_ml/include/woz_ml_los_features.h`](woz_ml_los_features.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_approach.c`](../modules.woz_aliro.src/aliro_approach.c.md), [`modules/woz_ml/src/woz_ml_feat.c`](../modules.woz_ml.src/woz_ml_feat.c.md), [`modules/woz_ml/src/woz_ml_lin.c`](../modules.woz_ml.src/woz_ml_lin.c.md), [`modules/woz_ml/src/woz_ml_los.c`](../modules.woz_ml.src/woz_ml_los.c.md), [`modules/woz_ml/src/woz_ml_range.c`](../modules.woz_ml.src/woz_ml_range.c.md)

## API

### `struct woz_ml_cia`
`modules/woz_ml/include/woz_ml.h:225`

The Ipatov CIA diagnostics the features are computed from, as read.
A struct rather than five uint32_t parameters precisely because they are five
same-typed integers: positional arguments there are a silent mis-order waiting
to happen, and like the feature vector itself a mis-ordered read produces a
confident wrong class rather than an error. Field for field from
dwt_rxdiag_t, so a caller writes .f1 = d.ipatovF1 and can check it by eye.
woz_ml deliberately does not include deca_device_api.h. That header is
LicenseRef-QORVO-2 and is only present on targets that carry the driver, while
this module is built and tested on the host with no driver at all.
