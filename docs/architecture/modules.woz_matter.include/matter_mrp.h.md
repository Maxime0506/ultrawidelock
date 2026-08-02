<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_mrp.h`

@file matter_mrp.h — Message Reliability Protocol: backoff, retransmit, dedup.
Matter runs over UDP, so reliability is the application's problem. MRP is the
answer: mark a message as needing an acknowledgement, retransmit on an
exponential backoff until it is acked, and drop counters you have already
seen.
Two objects with two different lifetimes, deliberately not merged:
struct matter_mrp_window  per SESSION   — duplicate suppression
struct matter_mrp         per EXCHANGE  — one un-acked message, one owed ack
NO TIMERS LIVE HERE. Every entry point takes `now_ms` and the object only
ever computes deadlines, so the caller owns the timer and this layer stays
testable on the host with a fake clock. That is also the stage 0 work-queue
constraint honoured by construction: a module that never arms a timer cannot
accidentally arm one on k_sys_work_q, which was measured at 3,568 of 4,096
bytes with the reader running.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_exchange.h`](matter_exchange.h.md), [`modules/woz_matter/src/matter_mrp.c`](../modules.woz_matter.src/matter_mrp.c.md)

## API

### `struct matter_mrp_window`
`modules/woz_matter/include/matter_mrp.h:122`

Replay window over a peer's message counters.
`bitmap` bit i records that counter `max_counter - (i + 1)` has been seen, so
a message up to MATTER_MRP_WINDOW_BITS behind the high-water mark is still
placed exactly. Anything older than that is refused, because there is no
longer any evidence either way and accepting is the unsafe guess.

### `struct matter_mrp`
`modules/woz_matter/include/matter_mrp.h:156`

Per-exchange reliability state.
At most ONE un-acked message at a time, which is the protocol's own rule and
not a simplification: MRP has no send window, so an exchange with a message
in flight must wait (CircuitMatter enforces the same thing at
exchange.py:67-68). The exchange layer owns the message bytes; this struct
holds only the counter and the deadlines.
