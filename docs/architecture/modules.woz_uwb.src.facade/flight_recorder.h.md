<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/flight_recorder.h`

@file flight_recorder.h
Capture and replay UWB frames and session configuration from a walk-up to a host for analysis and
replay. Records endpoint identity, status registers, frame data, and timing metadata into a
fixed-size ring buffer; provides reader and writer interfaces for host tools.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](woz_uwb_facade.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md)  ·  **discussed in** [`docs/range-integrity.md`](../../range-integrity.md)

## API

### `struct fr_meta`
`modules/woz_uwb/src/facade/flight_recorder.h:93`

Metadata header for a flight recorder stream: protocol version, port identifier (target), and the
firmware commit SHA1.

### `struct fr_config`
`modules/woz_uwb/src/facade/flight_recorder.h:104`

Captured Aliro session configuration snapshot: channel, timing parameters, STS index, URSK, and
responder credentials. Populated once at the start of each walk-up and replayed to reconstruct
session state during offline analysis.

### `struct fr_ev`
`modules/woz_uwb/src/facade/flight_recorder.h:123`

Single UWB event captured during a walk-up: endpoint identity, status register, frame length,
Ipatov and TX timestamps, STS quality metrics, and up to FR_FRAME_MAX bytes of the received
frame.

### `struct fr_end`
`modules/woz_uwb/src/facade/flight_recorder.h:141`

Trailer record marking the end of a flight recorder session: the count of captured events and a
truncation flag (1 if the ring filled and later events were dropped).

### `struct fr_record`
`modules/woz_uwb/src/facade/flight_recorder.h:150`

One record in a flight recorder stream: a discriminated union holding either a metadata header,
session configuration snapshot, a captured event, or the end-of-session trailer.

### `fr_writer_t`
`modules/woz_uwb/src/facade/flight_recorder.h:165`

─ Writer: append records to a fixed caller-owned buffer ─────────────────
Every fr_write_* returns 0 on success or -1 if the record did not fit; on the
first non-fit the writer latches `overflow` and all further writes are no-ops,
so the buffer always holds a valid record prefix (never a half-written tail).

### `fr_reader_t`
`modules/woz_uwb/src/facade/flight_recorder.h:183`

─ Reader: iterate a trace buffer ────────────────────────────────────────
fr_read_next fills *out and returns the record type (>0), 0 at clean end, or
-1 on a malformed/short/oversized/version-mismatched stream. The first record
of a well-formed trace is FR_REC_META and must carry FR_VERSION.

### `static inline void fr_set_enabled(bool on)`
`modules/woz_uwb/src/facade/flight_recorder.h:219`

Stub callback that enables or disables flight recording.
No-op when the flight recorder is disabled.

### `static inline bool fr_enabled(void)`
`modules/woz_uwb/src/facade/flight_recorder.h:227`

Returns true if the flight recorder is enabled and capturing events; false otherwise.
Stub when disabled.

### `static inline void fr_capture_config(const struct woz_uwb_aliro_cfg *cfg)`
`modules/woz_uwb/src/facade/flight_recorder.h:235`

Stub callback invoked when the flight recorder captures the Aliro session configuration.
No-op when the flight recorder is disabled.

### `static inline void fr_capture_ev(uint8_t ep, uint32_t status, uint16_t datalength)`
`modules/woz_uwb/src/facade/flight_recorder.h:243`

Stub callback invoked when the flight recorder captures a UWB event (endpoint fire, status, or
frame length). No-op when the flight recorder is disabled.

### `static inline void fr_dump(void)`
`modules/woz_uwb/src/facade/flight_recorder.h:253`

Stub callback that dumps the flight recorder ring buffer to the host interface.
No-op when the flight recorder is disabled.

### `static inline void fr_clear(void)`
`modules/woz_uwb/src/facade/flight_recorder.h:260`

Stub callback that clears the flight recorder ring buffer.
No-op when the flight recorder is disabled.
