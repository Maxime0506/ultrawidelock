<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_mrp.c`

@file matter_mrp.c — MRP backoff schedule, retransmit state, replay window.

**depends on** [`modules/woz_matter/include/matter_mrp.h`](../modules.woz_matter.include/matter_mrp.h.md)

## API

### `static int32_t elapsed(uint32_t now, uint32_t since)`
`modules/woz_matter/src/matter_mrp.c:45`

Milliseconds from `since` to `now`, correct across the uint32 wrap.

**called by** `matter_mrp_next_deadline`, `reached`

### `static bool reached(uint32_t now, uint32_t deadline)`
`modules/woz_matter/src/matter_mrp.c:51`

True when `now` has reached or passed `deadline`.

**called by** `matter_mrp_poll`  ·  **calls** `elapsed`

### `uint32_t matter_mrp_backoff_ms(uint32_t base_ms, uint8_t send_count, uint8_t jitter)`
`modules/woz_matter/src/matter_mrp.c:60`

Compute the MRP backoff delay in milliseconds given a base interval, send count, and random
jitter, applying exponential backoff with saturation at the protocol maximum.

**called by** `matter_mrp_arm`

### `void matter_mrp_window_init(struct matter_mrp_window *w)`
`modules/woz_matter/src/matter_mrp.c:97`

Initialize an MRP replay detection window to accept the first message.

### `int matter_mrp_window_check(const struct matter_mrp_window *w, uint32_t counter)`
`modules/woz_matter/src/matter_mrp.c:111`

Check whether a message counter is a replay, a new message, or too old to verify against the
sliding window; does not update the window.

### `void matter_mrp_window_commit(struct matter_mrp_window *w, uint32_t counter)`
`modules/woz_matter/src/matter_mrp.c:145`

Update the sliding window to mark a message counter as seen and advance the high-water mark;
handles wrapping and slide-off of old counters.

### `void matter_mrp_init(struct matter_mrp *m, uint32_t base_ms)`
`modules/woz_matter/src/matter_mrp.c:194`

Initialize an MRP state machine with a base retransmission interval in milliseconds.

### `int matter_mrp_arm(struct matter_mrp *m, uint32_t counter, uint32_t now_ms, uint8_t jitter)`
`modules/woz_matter/src/matter_mrp.c:213`

Arm MRP transmission tracking for a datagram counter: record the counter and send count, compute
the backoff delay with SENDER_BOOST, and store the time due.

**calls** `matter_mrp_backoff_ms`

### `int matter_mrp_on_ack(struct matter_mrp *m, uint32_t counter)`
`modules/woz_matter/src/matter_mrp.c:248`

Mark an in-flight message as acknowledged if the counter matches; returns MATTER_E_STATE if no
message is pending or MATTER_E_INVAL if the counter does not match.

### `void matter_mrp_on_reliable_recv(struct matter_mrp *m, uint32_t counter, uint32_t now_ms)`
`modules/woz_matter/src/matter_mrp.c:271`

Record receipt of a reliable message and arm the standalone acknowledgment timeout; a second
reliable message before the first ack goes out replaces the owed counter.

### `bool matter_mrp_take_ack(struct matter_mrp *m, uint32_t *counter)`
`modules/woz_matter/src/matter_mrp.c:288`

Retrieve and clear the pending acknowledgment counter if one is due; returns false if no
acknowledgment is pending.

### `enum matter_mrp_action matter_mrp_poll(struct matter_mrp *m, uint32_t now_ms, uint32_t *counter)`
`modules/woz_matter/src/matter_mrp.c:304`

Poll for the next MRP action to take: retransmit a message, send a standalone acknowledgment, or
remain idle; returns MATTER_MRP_GIVE_UP if max retransmissions reached.

**calls** `reached`

### `bool matter_mrp_next_deadline(const struct matter_mrp *m, uint32_t *out_ms)`
`modules/woz_matter/src/matter_mrp.c:334`

Return the next MRP deadline and whether any retransmission or acknowledgment is pending; used to
schedule the next poll.

**calls** `elapsed`
