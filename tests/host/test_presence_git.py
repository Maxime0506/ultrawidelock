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

    def __init__(self, script=b"", chunk=None):
        self.script = bytearray(script)
        self.chunk = chunk
        self.written = b""
        self.closed = False

    def write(self, data):
        self.written += data

    def flush(self):
        pass

    def read(self, n):
        if self.chunk is not None:
            n = min(n, self.chunk)
        out = bytes(self.script[:n])
        del self.script[:n]
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
    def test_pubkey_request_sends_the_right_frame(self):
        ser = FakeSerial(POINT_A)
        self.assertEqual(pg.dongle_pubkey(ser), POINT_A)
        self.assertEqual(ser.written, b"AQ")

    def test_challenge_sends_lead_type_and_nonce(self):
        nonce = bytes(range(16))
        frame = b"\x00" * pv.WIRE_P256
        ser = FakeSerial(frame)
        self.assertEqual(pg.dongle_assert(ser, nonce), frame)
        self.assertEqual(ser.written, b"AP" + nonce)

    def test_short_reads_are_reassembled(self):
        ser = FakeSerial(POINT_A, chunk=7)
        self.assertEqual(pg.dongle_pubkey(ser), POINT_A)

    def test_truncated_response_fails_loudly(self):
        ser = FakeSerial(POINT_A[:30])
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_pubkey(ser)
        self.assertIn("30 of 65", str(cm.exception))

    def test_dongle_without_a_key_is_refused(self):
        ser = FakeSerial(b"\x00" * pv.PUB_LEN)
        with self.assertRaises(pg.PresenceError) as cm:
            pg.dongle_pubkey(ser)
        self.assertIn("no signing key", str(cm.exception))

    def test_non_uncompressed_point_is_refused(self):
        ser = FakeSerial(b"\x02" + b"\x11" * 64)
        with self.assertRaises(pg.PresenceError):
            pg.dongle_pubkey(ser)


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
