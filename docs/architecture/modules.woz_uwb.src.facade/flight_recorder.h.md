<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/flight_recorder.h`

*No module docstring. First commit: "flight-recorder: record/replay real UWB walk-ups".*

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](woz_uwb_facade.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md)

## API

### `fr_writer_t`
`modules/woz_uwb/src/facade/flight_recorder.h:137`

─ Writer: append records to a fixed caller-owned buffer ─────────────────
Every fr_write_* returns 0 on success or -1 if the record did not fit; on the
first non-fit the writer latches `overflow` and all further writes are no-ops,
so the buffer always holds a valid record prefix (never a half-written tail).

### `fr_reader_t`
`modules/woz_uwb/src/facade/flight_recorder.h:155`

─ Reader: iterate a trace buffer ────────────────────────────────────────
fr_read_next fills *out and returns the record type (>0), 0 at clean end, or
-1 on a malformed/short/oversized/version-mismatched stream. The first record
of a well-formed trace is FR_REC_META and must carry FR_VERSION.

<details><summary>Undocumented (11)</summary>

- `fr_meta`
- `fr_config`
- `fr_ev`
- `fr_end`
- `fr_record`
- `fr_set_enabled`
- `fr_enabled`
- `fr_capture_config`
- `fr_capture_ev`
- `fr_dump`
- `fr_clear`

</details>
