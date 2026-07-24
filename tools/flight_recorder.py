#!/usr/bin/env python3
"""flight_recorder.py — carry a recorded UWB walk-up off the device and turn it
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
"""

import os
import struct
import sys

FR_MAGIC = 0x31435246  # "FRC1" little-endian
FR_VERSION = 1

FR_REC_META = 1
FR_REC_CONFIG = 2
FR_REC_EV = 3
FR_REC_END = 4

FR_EP_TRY_PREPOLL = 1
FR_EP_RX_REARM = 2
FR_EP_TX_DONE = 3

EP_NAME = {
    FR_EP_TRY_PREPOLL: "try_prepoll",
    FR_EP_RX_REARM: "rx_rearm",
    FR_EP_TX_DONE: "tx_done",
}
PORT_NAME = {0: "host", 1: "nrf5340dk", 2: "esp32"}


class TraceError(Exception):
    """Malformed trace (bad magic, version, or truncated record)."""


class Trace:
    def __init__(self):
        self.meta = None      # dict: version, port, sha
        self.config = None    # dict: session_id, channel, ..., ursk, rc
        self.events = []      # list of dicts
        self.end = None       # dict: n_events, truncated


def _is_hex_line(s):
    return len(s) > 0 and len(s) % 2 == 0 and all(
        c in "0123456789abcdefABCDEF" for c in s)


def read_hex_from_log(text):
    """Concatenate the pure-hex `[FREC] <hex>` payload lines into bytes. The
    `[FREC] begin ...` / `[FREC] end` markers contain spaces so they are not
    pure hex and are skipped. Returns b"" if the log holds no FREC data."""
    out = bytearray()
    for raw in text.splitlines():
        i = raw.find("[FREC] ")
        if i < 0:
            continue
        payload = raw[i + len("[FREC] "):].strip()
        if _is_hex_line(payload):
            out += bytes.fromhex(payload)
    return bytes(out)


def load_trace_bytes(data):
    """Return raw trace bytes from either a binary `.frc` (starts with the magic)
    or a serial log carrying `[FREC]` hex lines."""
    if len(data) >= 4 and struct.unpack_from("<I", data, 0)[0] == FR_MAGIC:
        return data
    try:
        text = data.decode("utf-8", errors="replace")
    except Exception:  # pragma: no cover - decode with replace never raises
        return b""
    return read_hex_from_log(text)


def parse_trace(data):
    """Parse trace bytes into a Trace. Mirrors fr_read_next()."""
    if len(data) < 4 or struct.unpack_from("<I", data, 0)[0] != FR_MAGIC:
        raise TraceError("bad magic (not a flight-recorder trace)")
    tr = Trace()
    pos = 4
    n = len(data)
    while pos < n:
        if pos + 3 > n:
            raise TraceError("truncated record header at %d" % pos)
        rtype, plen = struct.unpack_from("<BH", data, pos)
        pos += 3
        if pos + plen > n:
            raise TraceError("truncated payload at %d" % pos)
        body = data[pos:pos + plen]
        pos += plen
        if rtype == FR_REC_META:
            ver, port, shalen = struct.unpack_from("<HHB", body, 0)
            if ver != FR_VERSION:
                raise TraceError("unsupported version %d" % ver)
            sha = body[5:5 + shalen].decode("ascii", errors="replace")
            tr.meta = {"version": ver, "port": port, "sha": sha}
        elif rtype == FR_REC_CONFIG:
            (sid, chan, sci, sdr, bdm, spr, sts0, t0) = struct.unpack_from(
                "<IBBHIBIQ", body, 0)
            o = struct.calcsize("<IBBHIBIQ")
            ursk = body[o:o + 32]
            o += 32
            (rc_len,) = struct.unpack_from("<H", body, o)
            o += 2
            rc = body[o:o + rc_len]
            tr.config = {
                "session_id": sid, "channel": chan, "sync_code_index": sci,
                "slot_duration_rstu": sdr, "block_duration_ms": bdm,
                "slot_per_round": spr, "sts_index0": sts0, "uwb_time_us": t0,
                "ursk": ursk, "rc": rc,
            }
        elif rtype == FR_REC_EV:
            (ep, status, dlen, rxts, txts, systime, sq_valid, sq_val, sq_ret,
             flen) = struct.unpack_from("<BIHQQIBhiH", body, 0)
            o = struct.calcsize("<BIHQQIBhiH")
            frame = body[o:o + flen]
            tr.events.append({
                "ep": ep, "status": status, "datalength": dlen,
                "rx_ts40": rxts, "tx_ts40": txts, "systime": systime,
                "stsq_valid": sq_valid, "stsq_val": sq_val, "stsq_ret": sq_ret,
                "frame": frame,
            })
        elif rtype == FR_REC_END:
            nev, trunc = struct.unpack_from("<IB", body, 0)
            tr.end = {"n_events": nev, "truncated": bool(trunc)}
        # unknown record types are skipped (forward-compat)
    return tr


def extract_frames(trace):
    """The received UWB frames, in order (deduped-preserving is the caller's job).
    Only frame bytes — no key material."""
    return [ev["frame"] for ev in trace.events if ev["frame"]]


def write_corpus(frames, outdir):
    """Write each distinct frame as frame_NNNN.bin under outdir. Returns the
    number of files written (duplicates collapse to one)."""
    os.makedirs(outdir, exist_ok=True)
    seen = set()
    written = 0
    for fr in frames:
        if fr in seen:
            continue
        seen.add(fr)
        path = os.path.join(outdir, "frame_%04d.bin" % written)
        with open(path, "wb") as f:
            f.write(fr)
        written += 1
    return written


def summarize(trace):
    """A human-readable one-session report."""
    lines = []
    if trace.meta:
        lines.append("flight recorder trace — v%d, port %s, fw %s" % (
            trace.meta["version"],
            PORT_NAME.get(trace.meta["port"], "?%d" % trace.meta["port"]),
            trace.meta["sha"] or "?"))
    else:
        lines.append("flight recorder trace (no META)")
    if trace.config:
        c = trace.config
        lines.append(
            "  session 0x%08x  ch=%d code=%d  sts_index0=0x%08x  "
            "slots/round=%d  rc=%dB" % (
                c["session_id"], c["channel"], c["sync_code_index"],
                c["sts_index0"], c["slot_per_round"], len(c["rc"])))
    counts = {}
    for ev in trace.events:
        name = EP_NAME.get(ev["ep"], "ep%d" % ev["ep"])
        counts[name] = counts.get(name, 0) + 1
    order = ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts))
    lines.append("  events: %d  (%s)" % (len(trace.events), order or "none"))
    frames = extract_frames(trace)
    lines.append("  frames: %d received (%d distinct)" % (
        len(frames), len(set(frames))))
    if trace.end:
        lines.append("  end: n_events=%d%s" % (
            trace.end["n_events"],
            "  TRUNCATED (ring filled)" if trace.end["truncated"] else ""))
    else:
        lines.append("  end: MISSING (capture cut off before dump)")
    return "\n".join(lines)


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("Usage: %s <capture.log | trace.frc> [corpus_dir]\n"
                         % os.path.basename(argv[0]))
        return 2
    src = argv[1]
    corpus_dir = argv[2] if len(argv) > 2 else None
    try:
        with open(src, "rb") as f:
            data = f.read()
    except OSError as e:
        sys.stderr.write("error: cannot read %s: %s\n" % (src, e))
        return 2

    raw = load_trace_bytes(data)
    if not raw:
        print("no [FREC] trace data in %s" % src)
        return 0
    try:
        trace = parse_trace(raw)
    except TraceError as e:
        sys.stderr.write("error: malformed trace: %s\n" % e)
        return 1

    print(summarize(trace))

    # A .log input yields a .frc sidecar so the trace can be replayed offline.
    if not (len(data) >= 4 and struct.unpack_from("<I", data, 0)[0] == FR_MAGIC):
        frc = os.path.splitext(src)[0] + ".frc"
        with open(frc, "wb") as f:
            f.write(raw)
        print("wrote %s (%d bytes)" % (frc, len(raw)))

    if corpus_dir:
        n = write_corpus(extract_frames(trace), corpus_dir)
        print("wrote %d corpus frame(s) to %s" % (n, corpus_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
