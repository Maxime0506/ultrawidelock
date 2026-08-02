<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_thread.h`

@file matter_thread.h — the seam between a commissioner's dataset and a radio.
matter_clusters.c is platform-agnostic C11 and the host suite compiles it
without Zephyr, so it cannot call OpenThread. It calls these two instead; the
port forwards them to otDatasetSetActiveTlvs() and otThreadGetDeviceRole(),
and the host suite substitutes a double whose answers a test can choose.
The split into start and wait is deliberate. Apple sends
AddOrUpdateThreadNetwork, then ArmFailSafe, then ConnectNetwork, and the
attach can begin at the first of those rather than the last -- a Thread
attach costs seconds and the round trips in between are free.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_clusters.h`](matter_clusters.h.md)

## API

### `struct matter_thread_peer`
`modules/woz_matter/include/matter_thread.h:93`

Where a subscriber can be reached, kept opaque on purpose.
A raw IPv6 address and port rather than an OpenThread type: this header is
the portable seam and the host suite builds it without any Thread stack.
