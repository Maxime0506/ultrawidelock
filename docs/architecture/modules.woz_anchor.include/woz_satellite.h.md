<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_satellite.h`

@file woz_satellite.h — the freshness gate around a second anchor's report.
woz_fusion.h answers "which side of the door is this phone on" given two
distances measured at the same moment. This file is what makes that question
safe to ask on a real door, where the second distance arrives over a link
that can be slow, lossy or absent.
Three rules, and they are all about what happens when the satellite is NOT
there:
1. A report older than `stale_ms` is not a report. Geometry from a stale
distance is worse than no geometry, because it looks authoritative.
2. No fresh report means UNKNOWN, and UNKNOWN PERMITS prediction. A satellite
that has gone quiet must degrade to exactly today's behaviour, never to a
door that will not open. The tree already argues this for the range gate:
"A mis-tuned floor that refuses to open a door locks a human out of their
house" (docs/range-integrity.md:50-53).
3. Only a POSITIVE outside verdict, or a failed triangle test, withholds. Both
are real evidence; absence is not. An unconfigured baseline counts as
absence rather than as a failed test, so a misconfigured board degrades to
today's behaviour instead of silently never predicting again.
WHICH ANCHOR IS WHICH is a mounting fact, not a code fact, so it is a config
field rather than an assumption. Getting it backwards inverts the verdict --
it would predict for people outside and withhold from people inside, which is
the exact opposite of the point. `self_is_inside` makes that a decision
someone had to write down.

**depends on** [`modules/woz_anchor/include/woz_fusion.h`](woz_fusion.h.md)  ·  **used by** [`modules/woz_anchor/src/woz_satellite.c`](../modules.woz_anchor.src/woz_satellite.c.md)

## API

### `struct woz_satellite`
`modules/woz_anchor/include/woz_satellite.h:46`

Latest report from the second anchor, plus everything needed to judge it.
Caller-owned; this module allocates nothing and starts no threads.
