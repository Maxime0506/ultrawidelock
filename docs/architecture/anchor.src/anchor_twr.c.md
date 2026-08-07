<!-- generated documentation — edit the source, not this file -->
# `anchor/src/anchor_twr.c`

*No module docstring. First commit: "anchor: two-anchor DS-TWR bench link (stage A)".*

**depends on** [`anchor/src/anchor_twr.h`](anchor_twr.h.md)

## API

### `static int32_t median_push(int32_t mm)`
`anchor/src/anchor_twr.c:116`

Push one raw distance, return the window's median.
Produces a value from the first sample rather than waiting for a full window,
because a bring-up run should print something. With an even number of valid
entries it takes the UPPER of the two middles instead of averaging them:
averaging is banned for the reason above, and upper is the correct side to err
on, because every known failure of the leading-edge detector is short.

**called by** `anchor_twr_responder_round`

### `static void put_le32(uint8_t *p, uint32_t v)`
`anchor/src/anchor_twr.c:216`

@brief Store a uint32 little-endian.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`

### `static uint32_t get_le32(const uint8_t *p)`
`anchor/src/anchor_twr.c:225`

@brief Load a uint32 little-endian.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`

### `static uint64_t ts5_to_u64(const uint8_t t[5])`
`anchor/src/anchor_twr.c:232`

@brief Assemble a 5-byte DW3000 (40-bit) timestamp into a uint64 of DTU ticks.

**called by** `rx_ts`, `tx_ts`

### `static uint64_t rx_ts(void)`
`anchor/src/anchor_twr.c:239`

@brief Read the last RX RMARKER as DTU. STS is off here, so Ipatov is the only path.

**called by** `rx_expect`  ·  **calls** `ts5_to_u64`

### `static uint64_t tx_ts(void)`
`anchor/src/anchor_twr.c:248`

@brief Read the last TX RMARKER as DTU.

**called by** `anchor_twr_responder_round`, `note_tx_prediction`, `tx_now`  ·  **calls** `ts5_to_u64`

### `static uint32_t wait_status(uint32_t mask, int ceil_ms)`
`anchor/src/anchor_twr.c:267`

@brief Spin on SYS_STATUS until one of @p mask is set, or @p ceil_ms expires.
A busy poll, on purpose. Sleeping between reads would be kinder to the SPI bus
and would also add its whole period to the gap between a POLL landing and the
reply being scheduled -- and that gap is the one thing this protocol cannot
spend, because the reply has to be programmed before its slot arrives. There
is nothing else for this app to do with the CPU.
@return the status word; the caller checks which bit it got.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`, `rx_expect`, `tx_now`

### `static int rx_expect(uint8_t type, uint64_t *ts_out, int ceil_ms)`
`anchor/src/anchor_twr.c:291`

@brief Receive one frame of the expected type into g_rx.
@param type Expected message type byte.
@param ts_out RX RMARKER in DTU, written on success.
@param ceil_ms How long to spin before giving up. The two callers want very
different values: a reply leg knows the frame is one reply delay away,
while the responder waiting for the next POLL is waiting a whole round
period and must not keep dropping out of RX to be asked again.
@return frame length on success, -ETIMEDOUT on a quiet window, -EBADMSG otherwise.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`  ·  **calls** `rx_ts`, `wait_status`

### `static int tx_now(uint16_t len, uint64_t *ts_out)`
`anchor/src/anchor_twr.c:328`

@brief Transmit @p len bytes immediately and return the TX RMARKER.
@return 0 on success with @p ts_out written, -EIO if the frame never left.

**called by** `anchor_twr_initiator_round`  ·  **calls** `tx_ts`, `wait_status`

### `static uint32_t reply_hi32(uint64_t ref_dtu)`
`anchor/src/anchor_twr.c:356`

@brief The delayed-TX register value one reply delay after @p ref_dtu.
Single definition on purpose: the initiator needs this value BEFORE it writes
the FINAL payload (t5 goes inside the frame) and again when it arms the
transmit, and two copies of the arithmetic that must agree exactly is how the
two drift apart under a later edit.

**called by** `anchor_twr_initiator_round`, `tx_delayed`

### `static int tx_delayed(uint64_t ref_dtu, uint16_t len, uint64_t *pred_out)`
`anchor/src/anchor_twr.c:393`

@brief Schedule a delayed transmit one reply delay after @p ref_dtu.
The RMARKER the chip will produce is (dx << 8) plus whatever the TX antenna
delay register holds, so it is knowable BEFORE the frame goes out -- which is
what lets the initiator put t5 inside the FINAL it is about to send. Nothing
in this repo ever programs that register (deps/dw3000's only writers are in
the MCPS init path this stack does not use), so the offset is a constant, and
a constant is exactly what ANCHOR_ANT_DLY_DTU absorbs. The residual is
measured rather than assumed: every delayed TX compares the prediction with
the RMARKER the chip actually reports and keeps the worst gap in
anchor_twr_stats::t5_err_max.
@param ref_dtu Reference RMARKER (the frame that triggered this reply).
@param len Frame length to send, bytes, excluding FCS.
@param pred_out Predicted TX RMARKER in DTU.
@return 0 if the chip accepted the schedule, -ETIME if the slot was already past.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`  ·  **calls** `reply_hi32`

### `static void note_tx_prediction(uint64_t predicted)`
`anchor/src/anchor_twr.c:423`

@brief After a delayed TX completes, fold the prediction error into the stats.
Magnitude, not a signed difference: the antenna delay makes the actual RMARKER
LATER than the schedule, so the expected sign is known and only the size is
news. Taken as an absolute value rather than a wrapped subtraction, because a
wrap would turn "1 tick early" into 4.29 billion and hide the interesting case
behind an obviously-broken number.

**called by** `anchor_twr_initiator_round`, `anchor_twr_responder_round`  ·  **calls** `tx_ts`

### `static void anchor_hfclk_start(void)`
`anchor/src/anchor_twr.c:454`

Start the high-frequency crystal and wait for it.
WHY THIS APP NEEDS IT AND THE LOCK DOES NOT. The lock links MPSL for the
radio, and MPSL keeps the HFXO running for its own reasons -- the built
.config differs by exactly CONFIG_CLOCK_CONTROL_MPSL=y and
CONFIG_CLOCK_CONTROL_NRF_FORCE_ALT=y. This app has CONFIG_BT=n and no other
radio, so nothing asks for the crystal and the SoC stays on the internal RC
oscillator. Every DW3000 bring-up in this tree has therefore had the HFXO
running by accident rather than by request.
Blocking start, not the async one: this runs once at init with nothing else
to do, and a clock that is merely "starting" when the first SPI transfer goes
out is the situation being fixed.

**called by** `anchor_twr_init`

### `int anchor_twr_init(void)`
`anchor/src/anchor_twr.c:471`

Bring the radio up and apply the anchor PHY (SP0, STS off). 0 on success.

**calls** `anchor_hfclk_start`

### `int anchor_twr_initiator_round(uint32_t seq)`
`anchor/src/anchor_twr.c:509`

Run one initiator round. Returns 0 when the FINAL was transmitted.

**calls** `get_le32`, `note_tx_prediction`, `put_le32`, `reply_hi32`, `rx_expect`, `tx_delayed`, `tx_now`, `wait_status`

### `int anchor_twr_responder_round(int32_t *mm_out, uint32_t *seq_out)`
`anchor/src/anchor_twr.c:563`

Wait for one responder round.
@param mm_out  accepted distance, mm (written only on 0 return).
@param seq_out round sequence the initiator stamped (written only on 0 return).
@return 0 on an accepted range, negative otherwise.

**calls** `get_le32`, `median_push`, `note_tx_prediction`, `put_le32`, `rx_expect`, `tx_delayed`, `tx_ts`, `wait_status`

### `void anchor_twr_stats(struct anchor_twr_stats *out)`
`anchor/src/anchor_twr.c:682`

Copy the counters out.
