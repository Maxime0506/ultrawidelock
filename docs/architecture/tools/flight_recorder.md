<!-- generated documentation — edit the source, not this file -->
# `tools/flight_recorder.py`

flight_recorder.py — carry a recorded UWB walk-up off the device and turn it
into replayable / fuzzable artifacts.

The firmware's `fr dump` console command hex-encodes its RAM ring as `[FREC]`
serial lines (see modules/woz_uwb/src/facade/flight_recorder.c). This tool:

  * reconstructs the binary trace from those lines (or reads a `.frc` directly),
  * prints a human summary of the recorded session,
  * extracts the received UWB frames into a fuzz corpus (seeding
    tests/host/fuzz with genuine RF sessions).

Only the frames (already on-air ciphertext) go to the corpus — never the CONFIG
record's URSK, so a shared corpus carries no session key material.

Usage:
  flight_recorder.py <capture.log | trace.frc> [corpus_dir]

With a `.log` input the reconstructed trace is written next to it as `.frc`.
With a corpus_dir the frames are written there as `frame_NNNN.bin`. Stdlib only;
the binary format mirrors flight_recorder.h byte for byte.

## API

### `class TraceError(Exception)`
`tools/flight_recorder.py:48`

Malformed trace (bad magic, version, or truncated record).

**called by** `parse_trace`

### `read_hex_from_log(text)`
`tools/flight_recorder.py:65`

Concatenate the pure-hex `[FREC] <hex>` payload lines into bytes. The
`[FREC] begin ...` / `[FREC] end` markers contain spaces so they are not
pure hex and are skipped. Returns b"" if the log holds no FREC data.

**called by** `load_trace_bytes`  ·  **calls** `_is_hex_line`

### `load_trace_bytes(data)`
`tools/flight_recorder.py:80`

Return raw trace bytes from either a binary `.frc` (starts with the magic)
or a serial log carrying `[FREC]` hex lines.

**called by** `main`  ·  **calls** `read_hex_from_log`

### `parse_trace(data)`
`tools/flight_recorder.py:92`

Parse trace bytes into a Trace. Mirrors fr_read_next().

**called by** `main`  ·  **calls** `Trace`, `TraceError`

### `extract_frames(trace)`
`tools/flight_recorder.py:147`

The received UWB frames, in order (deduped-preserving is the caller's job).
Only frame bytes — no key material.

**called by** `main`, `summarize`

### `write_corpus(frames, outdir)`
`tools/flight_recorder.py:153`

Write each distinct frame as frame_NNNN.bin under outdir. Returns the
number of files written (duplicates collapse to one).

**called by** `main`

### `summarize(trace)`
`tools/flight_recorder.py:170`

A human-readable one-session report.

**called by** `main`  ·  **calls** `extract_frames`

<details><summary>Undocumented (4)</summary>

- `Trace`
- `Trace.__init__`
- `_is_hex_line`
- `main`

</details>
