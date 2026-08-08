<!-- generated documentation — edit the source, not this file -->
# `modules/woz_ml/src/woz_ml_log2.h`

@file woz_ml_log2.h — base-2 logarithm of an integer, without libm.
Internal to woz_ml and deliberately not in include/: it is not a general
numeric utility and its accuracy is only justified for the two callers it has.
Both of those callers want a logarithm of a positive integer and neither can
afford log10f. Pulling in libm would cost this module the one property its
README claims and `arm-none-eabi-nm -u` can check: not a single undefined
symbol, so the size that was measured is the whole size on every target
modules/ builds for.

**used by** [`modules/woz_ml/src/woz_ml_feat.c`](woz_ml_feat.c.md), [`modules/woz_ml/src/woz_ml_log2.c`](woz_ml_log2.c.md), [`modules/woz_ml/src/woz_ml_range.c`](woz_ml_range.c.md)
