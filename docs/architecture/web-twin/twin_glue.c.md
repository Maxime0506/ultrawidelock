<!-- generated documentation — edit the source, not this file -->
# `web-twin/twin_glue.c`

@file twin_glue.c — WASM entry points: the twin page's firmware harness.
Compiled (emcc) with the untouched modules/woz_uwb sources plus the same
tests/host shim the host suite links, so the page runs the real responder:
every block is a genuinely CCM*-encrypted Pre-POLL/POLL/Response/Final/
Final_Data exchange decoded by the firmware's own RX state machine, and the
page reads its decisions through the same facade seam the lock uses.
The peer (iPhone) side comes from tests/host/twin_frames.c — shared with
test_twin.c, so the page and the suite drive the responder identically. The
JS above supplies only the world: target distance, noise, spoof timing, and
the pacing of the five per-block legs (PREPOLL/POLL/TXDONE/FINAL/FINAL_DATA)
so a visitor can single-step a live DS-TWR round.
Distance is injected the way physics does it: the initiator-side DS-TWR
intervals ride in the Final_Data as round1 = reply1 + 2*tof and
reply2 = round2 - 2*tof, which makes the firmware's own
(round1*round2 - reply1*reply2)/sum recover exactly tof ticks
(1 tick ~ 15.65 ps, ~4.692 mm — ccc_shim_rx.c final_data_decode).
A Ghost-Peak spoof is a negative-tof block through the same full path.

**discussed in** [`web-twin/README.md`](../../../web-twin/README.md)

## API

### `EMSCRIPTEN_KEEPALIVE int twin_boot(void)`
`web-twin/twin_glue.c:66`

Boot the responder once per module instance (the page's Reset re-instantiates
the module, i.e. reboots the firmware). Returns 0 on success.

### `static int32_t tof_for_cm(int32_t cm)`
`web-twin/twin_glue.c:116`

Target distance -> ToF ticks, the inverse of final_data_decode's
d_mm = tof * 4692 / 1000 (rounded so the round-trip is stable).

**called by** `twin_step`

### `EMSCRIPTEN_KEEPALIVE int twin_step(int cm)`
`web-twin/twin_glue.c:133`

Run the next leg of the current ranging block against the live firmware:
0 Pre-POLL RX   — encrypted SP0 frame; RX event arms the SP3 POLL window
1 POLL result   — STS-only RFRAME at the armed index; fires Response_0 TX
2 Response TXFRS — t3 captured; the Final RX window is armed
3 Final RFRAME  — t6 + STS quality captured; radio reverts to SP0
4 Final_Data    — dUDSK-encrypted timestamps; DS-TWR computes and latches
@p cm is consumed at leg 4 (the initiator timestamps carry the distance).
Returns the leg just executed.

**called by** `twin_block`  ·  **calls** `tof_for_cm`

### `EMSCRIPTEN_KEEPALIVE void twin_block(int cm)`
`web-twin/twin_glue.c:181`

Run legs to the end of the current block (one full DS-TWR exchange).

**calls** `twin_step`

### `EMSCRIPTEN_KEEPALIVE int twin_leg(void)`
`web-twin/twin_glue.c:191`

Next leg within the block, 0..4 (0 = a fresh block is about to start).

### `EMSCRIPTEN_KEEPALIVE int twin_last_cm(void)`
`web-twin/twin_glue.c:197`

Facade telemetry seam: latest latched range, or TWIN_NO_RANGE.

### `EMSCRIPTEN_KEEPALIVE int twin_trusted_cm(void)`
`web-twin/twin_glue.c:205`

Facade unlock seam: the trusted range, or TWIN_NO_RANGE while trust is out.

### `EMSCRIPTEN_KEEPALIVE int twin_trust_level(void)`
`web-twin/twin_glue.c:213`

Layer-4 trust run length (0..K).

### `EMSCRIPTEN_KEEPALIVE int twin_trust_k(void)`
`web-twin/twin_glue.c:219`

FIRA_RANGE_TRUST_K, straight from the compiled firmware.

### `EMSCRIPTEN_KEEPALIVE int twin_plausible(int cm)`
`web-twin/twin_glue.c:225`

Layer-1 plausibility predicate (for the self-test).

### `EMSCRIPTEN_KEEPALIVE int twin_take_latches(void)`
`web-twin/twin_glue.c:232`

Accepted-latch count since last asked (the app_main task-notify analogue);
reading clears it. A rejected block never wakes this seam.

### `EMSCRIPTEN_KEEPALIVE int twin_awaiting_poll(void)`
`web-twin/twin_glue.c:241`

True while the SP3 RX is armed for the POLL (ccc_shim_rx state).

### `EMSCRIPTEN_KEEPALIVE unsigned twin_stat_rxenable(void)`
`web-twin/twin_glue.c:247`

Radio-stub call counters + wire-side identities, for the debug panel.

<details><summary>Undocumented (4)</summary>

- `twin_on_latch`
- `twin_stat_starttx`
- `twin_poll_index`
- `twin_block_no`

</details>
