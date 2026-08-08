<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/driver/uwb_cirdiag.c`

@file uwb_cirdiag.c — CIA RX-diagnostics latch + [ALAB] emitter (channel-impulse Stage 0/1).
Split the work across the two contexts the ALAB contract demands: the RX callback only
latches registers into a snapshot (uwb_cirdiag_capture, plain stores + one SPI read), and a
task-side uwb_cirdiag_flush formats/prints the line. On the nRF the flush runs on the
sysworkq (uwb_rxdiag.c submits it); on the ESP32 the pinned ISR-service task calls it after
its IRQ drain loop, so capture and flush are sequential there. A seqlock covers the
one real race (nRF: a new capture preempting a flush mid-copy): torn snapshots are dropped,
the next reception re-latches.
Stage 1 adds an independently-armed windowed-CIR dump: when armed, capture also reads a
fixed window of Ipatov complex taps centred on the first-path index into the snapshot. The
taps are NOT printed on the RX/flush path — a full window is ~64 serial lines per reception,
enough blocking UART to overrun the ranging slot and stall a live walk-up. Instead flush
appends each window to a small RAM ring (the last CIRDIAG_RING_RECS receptions), and the taps
are drained to `ev=uwb.cir` lines only when the dump is disarmed (uwb_cirdiag_dump_set_enabled
(false)) — that runs in console/task context after the walk-up, so the unlock is unaffected
while capturing. Deferring the printing was necessary but not sufficient: the window READ is
itself too long to sit inside a live ranging block, where the responder still owes a POLL or
Final reception. The shims pass that down as deadline_pending and the window is taken only on
the Final. Nor was that sufficient: the accumulator cannot be read at all while the receiver
is up, and the shim re-arms an SP0 listen the moment the Final is serviced, so the read has to
happen BEFORE that (the shims gate it on ccc_shim_rx_awaiting_final). Doing it on every block
then cost every range, so uwb_cirdiag_window_due decimates it to one Final in
CIRDIAG_CIR_EVERY.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](../modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md)  ·  **discussed in** [`ai/README.md`](../../../ai/README.md), [`ai/tinyml/PLAN.md`](../../../ai/tinyml/PLAN.md), [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md), [`modules/woz_ml/README.md`](../../../modules/woz_ml/README.md)

## API

### `static bool cirdiag_summary_due(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:109`

@brief True if this reception's summary is due. Always true at the default N of 1, so
every existing target keeps latching every reception.

**called by** `uwb_cirdiag_capture`

### `static uint16_t cirdiag_window_base(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:121`

@brief Absolute Ipatov sample index of tap 0 for a window centred on the latched first path,
clamped into the valid accumulator span [0, DWT_CIR_LEN_IP_PRF64 - WIN].

**called by** `uwb_cirdiag_capture`, `uwb_cirdiag_probe`

### `struct cirdiag_rec`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:149`

Channel impulse response diagnostic record: timestamp (microseconds), sample count, base index,
and tap array (I/Q magnitude or signed values) for post-processing.

### `static void cirdiag_drain(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:163`

@brief Emit every buffered window as `ev=uwb.cir` lines (oldest first), then empty the ring.
Called on dump disarm, off the ranging path. Blocking: up to RECS*WIN serial lines, and it
SLEEPS between records when CONFIG_WOZ_UWB_CIRDIAG_DRAIN_PACE_MS is set, so like the rest of
the disarm path this is thread context only, never an ISR.

**called by** `uwb_cirdiag_dump_set_enabled`

### `static void cirdiag_ring_append(int64_t t_us, uint32_t n, uint16_t base, const uint32_t *taps)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:198`

@brief Append one window to the ring, overwriting the oldest.
Called from the capture that just read the accumulator, NOT from the deferred flush. The flush
ran on a work item and read a "window valid" flag that every subsequent capture cleared,
so on any target where a second frame lands inside one workqueue latency the append never fired
at all and the ring stayed empty. That is every CDK round: CONFIG_WOZ_UWB_FINAL_SNAPSHOT arms a
delayed SP0 RX at the Final_Data slot, so a reception follows the Final by ~1 slot. Appending
here also makes the single-producer claim above true, since only this path is gated on g_dump.

**called by** `uwb_cirdiag_capture`

### `uint32_t uwb_cirdiag_ring_count(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:215`

Return the count of CIR windows currently in the ring buffer (0 to CIRDIAG_RING_RECS).

### `void uwb_cirdiag_set_enabled(bool on)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:223`

Enable or disable CIA diagnostic capture globally.

### `bool uwb_cirdiag_enabled(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:231`

Return true if CIA diagnostic capture is enabled.

### `void uwb_cirdiag_dump_set_enabled(bool on)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:240`

Arm or disarm CIR window capture. When arming, also arms the summary diagnostics. When disarming,
drains all buffered windows to the console via ev=uwb.cir lines and clears the ring.

**calls** `cirdiag_drain`

### `bool uwb_cirdiag_dump_enabled(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:259`

Return true if CIR window dump is armed (enabled and CIA logging armed).

### `bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending, bool is_final)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:270`

Capture one reception's CIR diagnostic snapshot: arm CIA logging on first RX, then latch the
status, frame length, STS quality/status, and (if window dump is enabled and the radio is idle) a
fixed-size Ipatov-centred CIR window. Returns true if capture succeeded; false on first RX or if
already pending. Seqlock-protected; safe to call from RX callback.

**calls** `cirdiag_ring_append`, `cirdiag_summary_due`, `cirdiag_window_base`

### `bool uwb_cirdiag_window_due(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:408`

Return true on every CIRDIAG_CIR_EVERY-th call if capture is enabled, CIA logging is armed, and
window dump is armed; used to throttle window reads during streaming.

### `void uwb_cirdiag_probe(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:422`

Diagnostic: read and print the CIR at four sample offsets (three distinct addresses plus one
repeat) to verify addressing and detect non-determinism. Requires CIA logging armed (one
reception taken). Outputs one pass in MID mode (int16 real/imag) and one in FULL mode (raw
24-bit) at the base offset.

**calls** `cirdiag_window_base`

### `static void cirdiag_range_fields(char *buf, size_t buf_len)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:494`

Format the DS-TWR distance as ` d=<cm> dage=<ms>`, or an empty string when there is none.
WHY THE KEYS ARE ABSENT RATHER THAN ZERO OR -1, which is the same rule the LEAN summary
below states for its own fields: a parser cannot tell `d=0` meaning "measured zero" from
`d=0` meaning "no round has completed yet", and one of those is a data point while the
other is a lie. An absent key is neither.
`dage` is not decoration. A DS-TWR round completes on its own cadence while diagnostics
are read per reception, so most receptions carry a range from a previous round, and how
stale it is decides whether the row belongs in a fitted set at all. The alternative the
host used before this existed -- scraping the bench TUI's rendered status line and
aligning on its reception counter -- had to drop 12 of 556 receptions that fell outside
any fresh line, and could not say how stale the ones it kept were.

**called by** `uwb_cirdiag_flush`

### `void uwb_cirdiag_flush(void)`
`modules/woz_uwb/src/driver/uwb_cirdiag.c:512`

Emit the pending CIR snapshot: write the summary line ([ALAB] ev=uwb.diag) with Ipatov and STS
peak/power/quality fields. The window itself is not handled here — the capture appends it to the
ring, because it does not survive to this work item. Retry up to 3 times if the seqlock detects
concurrent capture. Idempotent.

**calls** `cirdiag_range_fields`

<details><summary>Undocumented (1)</summary>

- `uwb_cirdiag_last_ipatov`

</details>
