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

SECURITY: raw serial logs containing `[FREC]` records and binary `.frc` files
contain the CONFIG record's full ephemeral URSK. Keep them private and do not
attach them to public issues. Only the extracted frame corpus excludes the key.

Usage:
  flight_recorder.py <capture.log | trace.frc> [corpus_dir]

With a `.log` input the reconstructed trace is written next to it as `.frc`.
With a corpus_dir the frames are written there as `frame_NNNN.bin`. Stdlib only;
the binary format mirrors flight_recorder.h byte for byte.

**discussed in** [`README.md`](../../../README.md), [`SECURITY.md`](../../../SECURITY.md), [`docs/configuring.md`](../../configuring.md)

## API

### `class TraceError(Exception)`
`tools/flight_recorder.py:52`

Malformed trace (bad magic, version, or truncated record).

**called by** `parse_trace`

### `class Trace`
`tools/flight_recorder.py:56`

In-memory representation of a single UWB walk-up trace: session metadata, radio configuration, received frames, and end-of-trace marker.

**called by** `parse_trace`

#### `Trace.__init__(self)`
`tools/flight_recorder.py:58`

Initialize an empty trace with null metadata, config, and end marker, and an empty frame list.

### `_is_hex_line(s)`
`tools/flight_recorder.py:66`

Return true if the string is a valid even-length sequence of hexadecimal digits.

**called by** `read_hex_from_log`

### `read_hex_from_log(text)`
`tools/flight_recorder.py:72`

Concatenate the pure-hex `[FREC] <hex>` payload lines into bytes. The
`[FREC] begin ...` / `[FREC] end` markers contain spaces so they are not
pure hex and are skipped. Returns b"" if the log holds no FREC data.

**called by** `load_trace_bytes`  ·  **calls** `_is_hex_line`

### `load_trace_bytes(data)`
`tools/flight_recorder.py:87`

Return raw trace bytes from either a binary `.frc` (starts with the magic)
or a serial log carrying `[FREC]` hex lines.

**called by** `main`  ·  **calls** `read_hex_from_log`

### `parse_trace(data)`
`tools/flight_recorder.py:99`

Parse trace bytes into a Trace. Mirrors fr_read_next().

**called by** `main`  ·  **calls** `Trace`, `TraceError`

### `extract_frames(trace)`
`tools/flight_recorder.py:154`

The received UWB frames, in order (deduped-preserving is the caller's job).
Only frame bytes — no key material.

**called by** `main`, `summarize`

### `write_corpus(frames, outdir)`
`tools/flight_recorder.py:160`

Write each distinct frame as frame_NNNN.bin under outdir. Returns the
number of files written (duplicates collapse to one).

**called by** `main`

### `summarize(trace)`
`tools/flight_recorder.py:177`

A human-readable one-session report.

**called by** `main`  ·  **calls** `extract_frames`

### `main(argv)`
`tools/flight_recorder.py:212`

Parse a capture log or binary trace file, print a summary, optionally write a .frc sidecar, and optionally extract frames into a corpus directory.

**calls** `extract_frames`, `load_trace_bytes`, `parse_trace`, `summarize`, `write_corpus`
