#!/usr/bin/env python3
"""Unit tests for tools/flight_recorder.py.

Runs against the committed golden trace (tests/host/data/flight_recorder_sample.frc,
produced by the C recorder itself so this cross-checks the two implementations
byte for byte) plus synthetic traces that drive the malformed / truncated /
serial-log / corpus paths. Stdlib only; run directly or via tests/host/run.sh."""

import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import flight_recorder as fr  # noqa: E402

SAMPLE = os.path.join(ROOT, "tests", "host", "data", "flight_recorder_sample.frc")


# ── synthetic trace builder (mirrors the flight_recorder.h byte layout) ──────

def _meta(version=fr.FR_VERSION, port=0, sha=b"abc1234"):
    body = struct.pack("<HHB", version, port, len(sha)) + sha
    return struct.pack("<BH", fr.FR_REC_META, len(body)) + body


def _config(sid=0x11223344, rc=b"\x00" * 17, ursk=b"\xA0" * 32):
    body = struct.pack("<IBBHIBIQ", sid, 9, 9, 1200, 192, 12, 0x400000, 0)
    body += ursk + struct.pack("<H", len(rc)) + rc
    return struct.pack("<BH", fr.FR_REC_CONFIG, len(body)) + body


def _ev(ep=fr.FR_EP_TRY_PREPOLL, status=0, frame=b"\x41\x42\x43"):
    body = struct.pack("<BIHQQIBhiH", ep, status, len(frame), 1, 2, 3, 1, 100,
                       0, len(frame)) + frame
    return struct.pack("<BH", fr.FR_REC_EV, len(body)) + body


def _end(n=1, trunc=0):
    body = struct.pack("<IB", n, trunc)
    return struct.pack("<BH", fr.FR_REC_END, len(body)) + body


def build(*records):
    return struct.pack("<I", fr.FR_MAGIC) + b"".join(records)


class GoldenTraceTest(unittest.TestCase):
    """The committed golden is what the C recorder emits for one DS-TWR round."""

    def setUp(self):
        with open(SAMPLE, "rb") as f:
            self.trace = fr.parse_trace(f.read())

    def test_meta(self):
        self.assertEqual(self.trace.meta["version"], fr.FR_VERSION)
        self.assertEqual(self.trace.meta["port"], 0)  # FR_PORT_HOST

    def test_config(self):
        c = self.trace.config
        self.assertEqual(c["session_id"], 0x11223344)
        self.assertEqual(c["channel"], 9)
        self.assertEqual(c["sync_code_index"], 9)
        self.assertEqual(c["sts_index0"], 0x400000)
        self.assertEqual(len(c["ursk"]), 32)
        self.assertEqual(len(c["rc"]), 17)

    def test_event_count_and_mix(self):
        self.assertEqual(len(self.trace.events), 8)
        eps = [e["ep"] for e in self.trace.events]
        self.assertEqual(eps.count(fr.FR_EP_TRY_PREPOLL), 4)
        self.assertEqual(eps.count(fr.FR_EP_RX_REARM), 3)
        self.assertEqual(eps.count(fr.FR_EP_TX_DONE), 1)

    def test_end_not_truncated(self):
        self.assertIsNotNone(self.trace.end)
        self.assertEqual(self.trace.end["n_events"], 8)
        self.assertFalse(self.trace.end["truncated"])

    def test_frames_extracted(self):
        frames = fr.extract_frames(self.trace)
        self.assertEqual(len(frames), 7)          # 7 events carry a frame
        self.assertEqual(len(set(frames)), 4)     # 4 distinct

    def test_summary_mentions_session(self):
        text = fr.summarize(self.trace)
        self.assertIn("0x11223344", text)
        self.assertIn("events: 8", text)
        self.assertIn("port host", text)


class SyntheticTest(unittest.TestCase):
    def test_roundtrip(self):
        data = build(_meta(port=2), _config(), _ev(), _ev(ep=fr.FR_EP_TX_DONE),
                     _end(n=2))
        t = fr.parse_trace(data)
        self.assertEqual(t.meta["port"], 2)
        self.assertEqual(len(t.events), 2)
        self.assertEqual(t.end["n_events"], 2)

    def test_bad_magic(self):
        with self.assertRaises(fr.TraceError):
            fr.parse_trace(b"\x00\x00\x00\x00rest")

    def test_bad_version(self):
        with self.assertRaises(fr.TraceError):
            fr.parse_trace(build(_meta(version=99)))

    def test_truncated_payload(self):
        data = build(_meta())
        with self.assertRaises(fr.TraceError):
            fr.parse_trace(data[:-2])  # chop the SHA tail

    def test_truncated_header(self):
        data = build(_meta()) + b"\x03\x10"  # a record type + 1 length byte
        with self.assertRaises(fr.TraceError):
            fr.parse_trace(data)

    def test_unknown_record_skipped(self):
        unknown = struct.pack("<BH", 99, 3) + b"\x01\x02\x03"
        t = fr.parse_trace(build(_meta(), unknown, _end()))
        self.assertIsNotNone(t.end)

    def test_truncation_flag(self):
        t = fr.parse_trace(build(_meta(), _config(), _ev(), _end(n=1, trunc=1)))
        self.assertTrue(t.end["truncated"])
        self.assertIn("TRUNCATED", fr.summarize(t))

    def test_missing_end(self):
        t = fr.parse_trace(build(_meta(), _config(), _ev()))
        self.assertIsNone(t.end)
        self.assertIn("MISSING", fr.summarize(t))


class SerialLogTest(unittest.TestCase):
    def _log(self, raw):
        # Render bytes the way `fr dump` does: begin/end markers + 32-byte hex.
        lines = ["boot noise", "[FREC] begin bytes=%d" % len(raw)]
        for i in range(0, len(raw), 32):
            lines.append("[FREC] " + raw[i:i + 32].hex())
        lines.append("[FREC] end")
        return "\n".join(lines) + "\n"

    def test_reconstruct_from_hex_lines(self):
        raw = build(_meta(), _config(), _ev(), _end())
        recovered = fr.read_hex_from_log(self._log(raw))
        self.assertEqual(recovered, raw)

    def test_markers_not_decoded(self):
        # "begin bytes=" and "end" must not be mistaken for hex data.
        raw = build(_meta(), _end())
        self.assertEqual(fr.read_hex_from_log(self._log(raw)), raw)

    def test_prefixed_frec_line(self):
        raw = build(_meta(), _end())
        log = "I (99) app: [FREC] " + raw.hex() + "\n"
        self.assertEqual(fr.read_hex_from_log(log), raw)

    def test_no_frec_data(self):
        self.assertEqual(fr.read_hex_from_log("just logs\nnothing\n"), b"")

    def test_load_trace_bytes_binary(self):
        raw = build(_meta(), _end())
        self.assertEqual(fr.load_trace_bytes(raw), raw)

    def test_load_trace_bytes_log(self):
        raw = build(_meta(), _end())
        self.assertEqual(fr.load_trace_bytes(self._log(raw).encode()), raw)


class CorpusTest(unittest.TestCase):
    def test_dedup_and_write(self):
        frames = [b"AAAA", b"BBBB", b"AAAA", b"CCCC"]
        with tempfile.TemporaryDirectory() as d:
            n = fr.write_corpus(frames, d)
            self.assertEqual(n, 3)  # AAAA collapses
            self.assertEqual(len(os.listdir(d)), 3)

    def test_corpus_excludes_ursk(self):
        # The corpus must contain only frame bytes, never the CONFIG ursk.
        secret = b"\xEE" * 32
        t = fr.parse_trace(build(_meta(), _config(ursk=secret), _ev(frame=b"\x11\x22"),
                                 _end()))
        with tempfile.TemporaryDirectory() as d:
            fr.write_corpus(fr.extract_frames(t), d)
            for name in os.listdir(d):
                with open(os.path.join(d, name), "rb") as f:
                    self.assertNotIn(secret, f.read())


class MainTest(unittest.TestCase):
    def run_main(self, data, want_corpus=False):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "in.frc")
            with open(path, "wb") as f:
                f.write(data)
            argv = ["flight_recorder.py", path]
            if want_corpus:
                argv.append(os.path.join(tmp, "corpus"))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = fr.main(argv)
            corpus_n = (len(os.listdir(os.path.join(tmp, "corpus")))
                        if want_corpus else 0)
            return rc, out.getvalue(), corpus_n

    def test_binary_input_ok(self):
        rc, term, _ = self.run_main(build(_meta(), _config(), _ev(), _end()))
        self.assertEqual(rc, 0)
        self.assertIn("flight recorder trace", term)

    def test_corpus_written(self):
        data = build(_meta(), _config(), _ev(frame=b"\x01\x02"),
                     _ev(frame=b"\x03\x04"), _end())
        rc, term, n = self.run_main(data, want_corpus=True)
        self.assertEqual(rc, 0)
        self.assertEqual(n, 2)
        self.assertIn("corpus frame", term)

    def test_log_writes_frc_sidecar(self):
        raw = build(_meta(), _config(), _ev(), _end())
        log = "[FREC] begin bytes=%d\n[FREC] %s\n[FREC] end\n" % (
            len(raw), raw.hex())
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "capture.log")
            with open(path, "w") as f:
                f.write(log)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = fr.main(["flight_recorder.py", path])
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.exists(os.path.join(tmp, "capture.frc")))
            self.assertIn("wrote", out.getvalue())

    def test_malformed_exit_one(self):
        rc, _, _ = self.run_main(struct.pack("<I", fr.FR_MAGIC)
                                 + struct.pack("<BH", fr.FR_REC_META, 5)
                                 + struct.pack("<HHB", 99, 0, 0))
        self.assertEqual(rc, 1)

    def test_empty_input_exit_zero(self):
        rc, term, _ = self.run_main(b"no frec here\n")
        self.assertEqual(rc, 0)
        self.assertIn("no [FREC] trace data", term)

    def test_usage_error(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(fr.main(["flight_recorder.py"]), 2)
        self.assertIn("Usage", err.getvalue())

    def test_missing_file(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = fr.main(["flight_recorder.py", "/nonexistent/x.frc"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
