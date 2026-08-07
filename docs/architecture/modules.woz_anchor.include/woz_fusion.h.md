<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_fusion.h`

*No module docstring. First commit: "woz_anchor: door geometry and two-anchor fusion, host-tested".*

**used by** [`modules/woz_anchor/include/woz_satellite.h`](woz_satellite.h.md), [`modules/woz_anchor/src/woz_fusion.c`](../modules.woz_anchor.src/woz_fusion.c.md)

## API

### `struct woz_fusion_cfg`
`modules/woz_anchor/include/woz_fusion.h:58`

Fixed install geometry plus the two tolerances, both sized from measured jitter.

### `struct woz_fusion_verdict`
`modules/woz_anchor/include/woz_fusion.h:78`

The verdict, with the evidence that produced it rather than just a yes or no.
