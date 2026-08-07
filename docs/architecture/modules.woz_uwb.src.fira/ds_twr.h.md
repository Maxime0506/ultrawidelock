<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/fira/ds_twr.h`

*No module docstring. First commit: "woz_uwb: one signed DS-TWR estimator, shared by both callers".*

**used by** [`modules/woz_uwb/src/ccc/ccc_mac.h`](../modules.woz_uwb.src.ccc/ccc_mac.h.md), [`modules/woz_uwb/src/fira/ds_twr.c`](ds_twr.c.md)

## API

### `struct ds_twr`
`modules/woz_uwb/src/fira/ds_twr.h:34`

@brief The four DS-TWR intervals, in ranging-timestamp ticks (wrap mod 2^32).
All four are differences of a 40-bit chip counter truncated to 32 bits, so
the wrap is deliberate: every interval is far shorter than the 2^32-tick
period, and the subtraction that produced it is correct across a wrap.
