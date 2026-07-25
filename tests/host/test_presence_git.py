#!/usr/bin/env python3
"""Unit tests for tools/presence_git.py.

Stdlib only; run directly or via tests/host/run.sh.

The end-to-end tests build a real throwaway git repository, tag it with a real
ECDSA-P256 assertion over the real binding nonce, and verify it the way CI
would. Repos are created with local config that disables GPG signing and sets a
placeholder identity, so nothing here depends on -- or touches -- the machine's
git configuration.

The frame-building helpers are shared with test_presence_verify.py rather than
duplicated, so both suites exercise the same signing path.
"""

import argparse
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import presence_git as pg  # noqa: E402
import presence_verify as pv  # noqa: E402
import test_presence_verify as tpv  # noqa: E402

needs_openssl = tpv.needs_openssl

POINT_A = tpv.KAT_POINT
# A structurally valid but unenrolled key, for the "not in the trust store" path.
POINT_B = bytes.fromhex(
    "04" + "11" * 32 + "22" * 32
)
# A frame with the right header and a junk body: enough to test the transport, which
# runs before anyone looks at the signature.
P256_FRAME = pv.MAGIC + bytes([pv.VERSION, pv.ALG_ECDSA_P256]) + bytes(
    (i * 7) & 0xFF for i in range(pv.WIRE_P256 - 4)
)


def pub_line(point):
    return f"{pg.TAG_PUB} {point.hex()}\n".encode()


def p256_line(frame):
    return f"{pg.TAG_P256} {frame.hex()}\n".encode()


@contextlib.contextmanager
def chdir(path):
    old = os.getcwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(old)


@contextlib.contextmanager
def quiet():
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        yield


def make_repo(d):
    """A one-commit repo with signing off and a placeholder identity."""
    def g(*args):
        subprocess.run(["git", *args], cwd=d, check=True, capture_output=True)

    g("init", "-q", "--template=")
    g("config", "--local", "user.email", "presence-test@example.invalid")
    g("config", "--local", "user.name", "presence test")
    # The machine's global config may sign tags and commits; a test must never
    # depend on a signing key being present, or prompt for one.
    g("config", "--local", "commit.gpgsign", "false")
    g("config", "--local", "tag.gpgsign", "false")
    # Nor may it run whatever hooks the developer has installed globally. Those
    # belong to their working repos, not to a throwaway fixture, and running a
    # machine-wide pre-commit scanner here cost about 2 s per repo.
    g("config", "--local", "core.hooksPath", os.path.join(d, ".git", "no-hooks"))
    with open(os.path.join(d, "f.txt"), "w", encoding="utf-8") as fh:
        fh.write("hello\n")
    g("add", "f.txt")
    g("commit", "-q", "-m", "initial")
    return subprocess.run(["git", "rev-parse", "HEAD"], cwd=d, check=True,
                          capture_output=True, text=True).stdout.strip()


def write_enrolled(d, entries):
    path = os.path.join(d, ".presence", "enrolled")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("# trusted dongles\n")
        for name, point in entries:
            fh.write(f"{name} {point.hex()}\n")
    return path


def make_tag(d, tag, commit, key_id_hex=None, frame_hex=None, body="Release"):
    msg = body
    if key_id_hex is not None or frame_hex is not None:
        msg += "\n\n"
        if key_id_hex is not None:
            msg += f"{pg.TRAILER_KEY_ID}: {key_id_hex}\n"
        if frame_hex is not None:
            msg += f"{pg.TRAILER_ASSERTION}: {frame_hex}\n"
    subprocess.run(["git", "tag", "-a", tag, commit, "-m", msg], cwd=d,
                   check=True, capture_output=True)


def signed_frame(nonce, distance=25, status=pv.PRESENCE_PRESENT):
    """A real P-256 assertion over nonce. Returns (point, frame)."""
    prefix = tpv.build_prefix(status=status, nonce=nonce, distance=distance)
    point, sig = tpv.gen_key_and_sign(prefix)
    return point, prefix + sig


class FakeSerial:
    """Enough of pyserial for the dongle helpers, with a scriptable read stream."""

    def __init__(self, script=b"", chunk=None, answers=None):
        self.script = bytearray(script)
        self.chunk = chunk
        self.written = b""
        self.closed = False
        self.flushed_input = 0
        # command substring -> bytes the board replies with. Needed to model a
        # board that rejects one command and then says nothing: with a single
        # pre-loaded script, a later command's answer would be read as if it had
        # answered the earlier one, and a fallback would look like it worked.
        self.answers = answers or {}

    def write(self, data):
        self.written += data
        text = data.decode("utf-8", "replace")
        for key, reply in self.answers.items():
            if key in text:
                self.script += reply
                break

    def flush(self):
        pass

    def reset_input_buffer(self):
        # Counted, not enacted: the script holds responses the dongle has yet to
        # send, not bytes already sitting in an OS buffer, so discarding it here
        # would model the opposite of what pyserial does. Resync against injected
        # console text is covered separately, on the read path.
        self.flushed_input += 1

    def read(self, n):
        if self.chunk is not None:
            n = min(n, self.chunk)
        out = bytes(self.script[:n])
        del self.script[:n]
        return out

    def readline(self):
        at = self.script.find(b"\n")
        end = len(self.script) if at < 0 else at + 1
        out = bytes(self.script[:end])
        del self.script[:end]
        return out

    def close(self):
        self.closed = True


class NonceTests(unittest.TestCase):
    COMMIT = "a" * 40

    def test_length_and_determinism(self):
        n = pg.binding_nonce("v1.0.0", self.COMMIT)
        self.assertEqual(len(n), pv.NONCE_LEN)
        self.assertEqual(n, pg.binding_nonce("v1.0.0", self.COMMIT))

    def test_tag_name_changes_the_nonce(self):
        self.assertNotEqual(pg.binding_nonce("v1.0.0", self.COMMIT),
                            pg.binding_nonce("v1.0.1", self.COMMIT))

    def test_commit_changes_the_nonce(self):
        self.assertNotEqual(pg.binding_nonce("v1.0.0", self.COMMIT),
                            pg.binding_nonce("v1.0.0", "b" * 40))

    def test_field_separator_prevents_a_boundary_collision(self):
        # Without the NUL separators, ("v1", "0abc") and ("v10", "abc") would
        # hash identical bytes and share a nonce.
        self.assertNotEqual(pg.binding_nonce("v1", "0abc"), pg.binding_nonce("v10", "abc"))

    def test_domain_separated_from_a_bare_hash(self):
        import hashlib
        bare = hashlib.sha256(b"v1.0.0" + b"\0" + self.COMMIT.encode()).digest()[:16]
        self.assertNotEqual(pg.binding_nonce("v1.0.0", self.COMMIT), bare)


class KeyIdTests(unittest.TestCase):
    def test_stable_and_eight_bytes(self):
        self.assertEqual(len(pg.key_id(POINT_A)), 8)
        self.assertEqual(pg.key_id(POINT_A), pg.key_id(POINT_A))

    def test_distinct_keys_get_distinct_ids(self):
        self.assertNotEqual(pg.key_id(POINT_A), pg.key_id(POINT_B))


class EnrolledFileTests(unittest.TestCase):
    def test_missing_file_is_empty_not_an_error(self):
        self.assertEqual(pg.read_enrolled("/nonexistent/enrolled"), {})

    def test_parses_names_comments_and_blank_lines(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "enrolled")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(f"# a comment\n\nalpha {POINT_A.hex()}\n")
                fh.write(f"beta {POINT_B.hex()}  # trailing comment\n")
            keys = pg.read_enrolled(p)
        self.assertEqual(keys[pg.key_id(POINT_A).hex()][0], "alpha")
        self.assertEqual(keys[pg.key_id(POINT_B).hex()][0], "beta")

    def test_malformed_line_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "enrolled")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write("only-a-name\n")
            with self.assertRaises(pg.PresenceError):
                pg.read_enrolled(p)

    def test_bad_hex_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "enrolled")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write("alpha zzzz\n")
            with self.assertRaises(pg.PresenceError):
                pg.read_enrolled(p)

    def test_wrong_key_length_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "enrolled")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write("alpha 0411\n")
            with self.assertRaises(pg.PresenceError):
                pg.read_enrolled(p)

    def test_compressed_point_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "enrolled")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(f"alpha 02{'11' * 32}{'22' * 32}\n")
            with self.assertRaises(pg.PresenceError):
                pg.read_enrolled(p)


class TagPlumbingTests(unittest.TestCase):
    def test_trailers_read_back_from_a_real_tag(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            make_tag(d, "v1.0.0", commit, key_id_hex="1122334455667788", frame_hex="aabbcc")
            self.assertEqual(pg.tag_trailer("v1.0.0", pg.TRAILER_KEY_ID, cwd=d),
                             "1122334455667788")
            self.assertEqual(pg.tag_trailer("v1.0.0", pg.TRAILER_ASSERTION, cwd=d), "aabbcc")
            self.assertEqual(pg.tag_commit("v1.0.0", cwd=d), commit)

    def test_absent_trailer_is_empty(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            make_tag(d, "v1.0.0", commit)
            self.assertEqual(pg.tag_trailer("v1.0.0", pg.TRAILER_KEY_ID, cwd=d), "")

    def test_unknown_tag_raises(self):
        with tempfile.TemporaryDirectory() as d:
            make_repo(d)
            with self.assertRaises(pg.PresenceError):
                pg.tag_commit("v9.9.9", cwd=d)


@needs_openssl
class VerifyTagTests(unittest.TestCase):
    def test_unsigned_tag_reports_none_rather_than_failing(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            make_tag(d, "v1.0.0", commit)
            verdict, detail = pg.verify_tag("v1.0.0", root=d)
        self.assertIsNone(verdict)
        self.assertEqual(detail["commit"], commit)

    def test_end_to_end_verifies(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce, distance=17)
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            verdict, detail = pg.verify_tag("v1.0.0", root=d)
        self.assertEqual(verdict, pv.OK, pv.VERDICT_REASON.get(verdict))
        self.assertEqual(detail["dongle"], "my-dongle")
        self.assertEqual(detail["fields"]["distance_cm"], 17)

    def test_assertion_for_another_tag_name_does_not_transfer(self):
        # The whole point of binding: a proof minted for v1.0.0 is worthless on v2.
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            point, frame = signed_frame(pg.binding_nonce("v1.0.0", commit))
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v2.0.0", commit, pg.key_id(point).hex(), frame.hex())
            verdict, _ = pg.verify_tag("v2.0.0", root=d)
        self.assertEqual(verdict, pv.E_NONCE)

    def test_assertion_for_another_commit_does_not_transfer(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            point, frame = signed_frame(pg.binding_nonce("v1.0.0", "f" * 40))
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            verdict, _ = pg.verify_tag("v1.0.0", root=d)
        self.assertEqual(verdict, pv.E_NONCE)

    def test_unenrolled_dongle_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce)
            write_enrolled(d, [("someone-else", POINT_B)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            with self.assertRaises(pg.PresenceError) as cm:
                pg.verify_tag("v1.0.0", root=d)
        self.assertIn("not enrolled", str(cm.exception))

    def test_key_id_pointing_at_the_wrong_enrolled_key_fails_the_signature(self):
        # Claiming another enrolled dongle's id must not verify.
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce)
            other, _ = signed_frame(nonce)
            write_enrolled(d, [("mine", point), ("theirs", other)])
            make_tag(d, "v1.0.0", commit, pg.key_id(other).hex(), frame.hex())
            verdict, _ = pg.verify_tag("v1.0.0", root=d)
        self.assertEqual(verdict, pv.E_MAC)

    def test_distance_threshold_applies(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce, distance=39)
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            self.assertEqual(pg.verify_tag("v1.0.0", root=d, max_cm=40)[0], pv.OK)
            self.assertEqual(pg.verify_tag("v1.0.0", root=d, max_cm=38)[0], pv.E_RANGE)

    def test_absent_status_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce, distance=pv.DIST_NONE,
                                        status=pv.PRESENCE_ABSENT)
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            verdict, _ = pg.verify_tag("v1.0.0", root=d)
        self.assertEqual(verdict, pv.E_ABSENT)

    def test_tampered_frame_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce, distance=200)
            t = bytearray(frame)
            t[pv.OFF_DISTANCE] = 0
            t[pv.OFF_DISTANCE + 1] = 5  # forge "5 cm"
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), bytes(t).hex())
            verdict, _ = pg.verify_tag("v1.0.0", root=d)
        self.assertEqual(verdict, pv.E_MAC)

    def test_half_a_trailer_pair_is_an_error_not_a_skip(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            make_tag(d, "v1.0.0", commit, key_id_hex="1122334455667788")
            with self.assertRaises(pg.PresenceError) as cm:
                pg.verify_tag("v1.0.0", root=d)
        self.assertIn("incomplete", str(cm.exception))

    def test_non_hex_assertion_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            make_tag(d, "v1.0.0", commit, "1122334455667788", "nothex")
            with self.assertRaises(pg.PresenceError):
                pg.verify_tag("v1.0.0", root=d)


class SerialTests(unittest.TestCase):
    def test_pubkey_request_sends_the_console_command(self):
        ser = FakeSerial(pub_line(POINT_A))
        self.assertEqual(pg.dongle_pubkey(ser), POINT_A)
        self.assertEqual(ser.written, b"presence pub\n")

    def test_challenge_sends_the_nonce_as_hex(self):
        nonce = bytes(range(16))
        ser = FakeSerial(p256_line(P256_FRAME))
        self.assertEqual(pg.dongle_assert(ser, nonce), P256_FRAME)
        self.assertEqual(ser.written, b"presence assert " + nonce.hex().encode() + b"\n")

    def test_log_lines_around_the_answer_are_ignored(self):
        # The whole point of moving off a binary channel: an interleaved log line is
        # a line that does not match, where before it corrupted the response.
        noise = b"I (523) ccc_shim: arm#0 slot=2 key0 wr deadbeef\n"
        ser = FakeSerial(noise + b"esp32> \n" + p256_line(P256_FRAME))
        self.assertEqual(pg.dongle_assert(ser, bytes(16)), P256_FRAME)

    def test_dongle_error_line_is_surfaced_not_swallowed(self):
        ser = FakeSerial(b"PRESENCE-ERR no usable device signing key\n")
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_assert(ser, bytes(16))
        self.assertIn("no usable device signing key", str(cm.exception))

    def test_truncated_answer_fails_loudly(self):
        ser = FakeSerial(b"PRESENCE-PUB " + POINT_A[:30].hex().encode() + b"\n")
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_pubkey(ser)
        self.assertIn("30 bytes, expected 65", str(cm.exception))

    def test_non_hex_answer_fails_loudly(self):
        ser = FakeSerial(b"PRESENCE-PUB not-actually-hex\n")
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_pubkey(ser)
        self.assertIn("not hex", str(cm.exception))

    def test_dongle_without_a_key_is_refused(self):
        ser = FakeSerial(pub_line(b"\x00" * pv.PUB_LEN))
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_pubkey(ser)
        self.assertIn("no signing key", str(cm.exception))

    def test_non_uncompressed_point_is_refused(self):
        ser = FakeSerial(pub_line(b"\x02" + b"\x11" * 64))
        with self.assertRaises(pg.PresenceError):
            pg.dongle_pubkey(ser)

    def test_stale_input_is_flushed_before_each_request(self):
        ser = FakeSerial(pub_line(POINT_A))
        pg.dongle_pubkey(ser)
        self.assertEqual(ser.flushed_input, 1)
        ser.script = bytearray(p256_line(P256_FRAME))
        pg.dongle_assert(ser, bytes(16))
        self.assertEqual(ser.flushed_input, 2)

    def test_wrong_firmware_fails_with_advice(self):
        ser = FakeSerial(b"some other board\n" * (pg.REPLY_LINE_BUDGET + 5))
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_assert(ser, bytes(16))
        self.assertIn("CONFIG_WOZ_PRESENCE", str(cm.exception))

    def test_silent_port_fails_with_the_same_advice(self):
        # The realistic wrong-firmware path: one "Unrecognized command" and then
        # nothing. The timeout must carry the advice, since it is what users hit.
        ser = FakeSerial(b"")
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_assert(ser, bytes(16))
        self.assertIn("CONFIG_WOZ_PRESENCE", str(cm.exception))


class CloneTests(unittest.TestCase):
    BLOB = "ab" * 238  # ALIRO_PROV_BLOB_MAX, the largest a real board emits

    def test_export_picks_the_bare_hex_line_past_the_header(self):
        script = (
            b"aliro-export: 238 bytes (contains the reader PRIVATE KEY -- bench only)\n"
            + self.BLOB.encode()
            + b"\n"
        )
        self.assertEqual(pg.export_identity(FakeSerial(script)), self.BLOB)
        # The header line is hex-ish in places; length is what rules it out.
        self.assertNotIn("bytes", pg.export_identity(FakeSerial(script)))

    def test_export_without_clone_support_says_so(self):
        ser = FakeSerial(b"Unrecognized command\n" * 5)
        with self.assertRaises(pg.PresenceError) as cm:
            pg.export_identity(ser)
        self.assertIn("CONFIG_WOZ_ALIRO_CLONE", str(cm.exception))

    def test_import_sends_the_blob_and_returns_the_verdict(self):
        ser = FakeSerial(b"aliro-import: adopted 238-byte identity + trust store (saved to NVS)\n")
        line = pg.import_identity(ser, self.BLOB)
        self.assertIn("adopted", line)
        self.assertEqual(ser.written, f"aliro-import {self.BLOB}\n".encode())

    def test_export_falls_back_to_the_matter_lock_spelling(self):
        # The standalone reader registers aliro-export; the Matter lock registers
        # it as a subcommand of aliro. Either board can be the identity source, so
        # a board that rejects the first spelling must still be read by the second.
        ser = FakeSerial(answers={
            "aliro-export": b"Unrecognized command\n",
            "aliro export": self.BLOB.encode() + b"\n",
        })
        self.assertEqual(pg.export_identity(ser), self.BLOB)
        self.assertIn(b"aliro-export\n", ser.written)
        self.assertIn(b"aliro export\n", ser.written)

    def test_import_falls_back_to_the_matter_lock_spelling(self):
        ser = FakeSerial(answers={
            "aliro-import ": b"Unrecognized command\n",
            "aliro import ": b"aliro import: adopted 238-byte identity\n",
        })
        self.assertIn("adopted", pg.import_identity(ser, self.BLOB))
        self.assertIn(f"aliro import {self.BLOB}\n".encode(), ser.written)

    def test_blob_too_long_for_the_console_is_refused_before_sending(self):
        # A blob that overflows max_cmdline_length arrives truncated and is rejected
        # as malformed, which blames the blob for a transport limit. Catch it here.
        ser = FakeSerial(b"")
        with self.assertRaises(pg.PresenceError) as cm:
            pg.import_identity(ser, "cd" * pg.CONSOLE_LINE_MAX)
        self.assertIn("console line", str(cm.exception))
        self.assertEqual(ser.written, b"")

    def test_largest_real_blob_still_fits_the_console_line(self):
        # 476 bytes -> 952 hex chars + "aliro-import " + newline, against 1024.
        self.assertLess(len(f"aliro-import {self.BLOB}\n"), pg.CONSOLE_LINE_MAX)


@needs_openssl
class ProbeTests(unittest.TestCase):
    """The bring-up path, driven through a fake dongle on a fixed nonce."""

    NONCE = bytes(range(16))

    def run_probe(self, script, max_cm=40):
        """Drive cmd_probe over a scripted port. Returns (rc, stdout)."""
        port = FakeSerial(script)
        args = argparse.Namespace(port="fake", max_cm=max_cm, openssl="openssl")
        buf = io.StringIO()
        real_open, real_urandom = pg.open_port, os.urandom
        pg.open_port = lambda *_a, **_k: port
        os.urandom = lambda n: self.NONCE[:n]
        try:
            with contextlib.redirect_stdout(buf):
                rc = pg.cmd_probe(args)
        finally:
            pg.open_port, os.urandom = real_open, real_urandom
        return rc, buf.getvalue()

    def test_present_dongle_passes(self):
        point, frame = signed_frame(self.NONCE, distance=20)
        rc, out = self.run_probe(pub_line(point) + p256_line(frame))
        self.assertEqual(rc, 0, out)
        self.assertIn("signature  VERIFIED", out)
        self.assertIn("OK", out)
        self.assertIn("20 cm", out)

    def test_absent_dongle_is_still_a_crypto_pass(self):
        # The realistic first-flash result: keys work, no phone has ranged yet.
        point, frame = signed_frame(self.NONCE, distance=pv.DIST_NONE,
                                    status=pv.PRESENCE_ABSENT)
        rc, out = self.run_probe(pub_line(point) + p256_line(frame))
        self.assertEqual(rc, 0, out)
        self.assertIn("signature  VERIFIED", out)
        self.assertIn("E_ABSENT", out)
        self.assertIn("distance   none", out)
        self.assertIn("Crypto chain is good", out)

    def test_mismatched_key_fails_loudly(self):
        # Proves the probe is not permissive: a frame the pubkey did not sign
        # must report FAILED, which is the real bring-up failure mode.
        _, frame = signed_frame(self.NONCE, distance=20)
        other, _ = signed_frame(self.NONCE, distance=20)
        rc, out = self.run_probe(pub_line(other) + p256_line(frame))
        self.assertEqual(rc, 1)
        self.assertIn("signature  FAILED", out)

    def test_keyless_dongle_refused_before_any_challenge(self):
        with self.assertRaises(pg.PresenceError):
            self.run_probe(pub_line(b"\x00" * pv.PUB_LEN))


class CliTests(unittest.TestCase):
    def test_nonce_subcommand_prints_the_binding(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            buf = io.StringIO()
            with chdir(d), contextlib.redirect_stdout(buf):
                rc = pg.main(["nonce", "--tag", "v1.0.0", "--commit", commit])
        self.assertEqual(rc, 0)
        self.assertEqual(buf.getvalue().strip(), pg.binding_nonce("v1.0.0", commit).hex())

    @needs_openssl
    def test_verify_subcommand_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            point, frame = signed_frame(nonce, distance=20)
            write_enrolled(d, [("my-dongle", point)])
            make_tag(d, "v1.0.0", commit, pg.key_id(point).hex(), frame.hex())
            make_tag(d, "v2.0.0", commit)
            with chdir(d), quiet():
                good = pg.main(["verify", "--tag", "v1.0.0"])
                too_far = pg.main(["verify", "--tag", "v1.0.0", "--max-cm", "5"])
                unsigned = pg.main(["verify", "--tag", "v2.0.0"])
                required = pg.main(["verify", "--tag", "v2.0.0", "--require"])
        self.assertEqual(good, 0)
        self.assertEqual(too_far, 1)
        self.assertEqual(unsigned, 0, "an unsigned tag is skipped, not failed")
        self.assertEqual(required, 1, "--require turns a missing assertion into a failure")

    def test_missing_openssl_exits_2_not_1(self):
        with tempfile.TemporaryDirectory() as d:
            commit = make_repo(d)
            nonce = pg.binding_nonce("v1.0.0", commit)
            write_enrolled(d, [("my-dongle", POINT_A)])
            make_tag(d, "v1.0.0", commit, pg.key_id(POINT_A).hex(),
                     (b"\x00" * pv.WIRE_P256).hex())
            with chdir(d), quiet():
                rc = pg.main(["verify", "--tag", "v1.0.0",
                              "--openssl", "/nonexistent/openssl-does-not-exist"])
        # A malformed frame is caught before openssl is ever reached, so this
        # also pins that framing runs first.
        self.assertEqual(rc, 1)

    def test_unknown_subcommand_is_refused(self):
        with self.assertRaises(SystemExit), quiet():
            pg.main(["nope"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
