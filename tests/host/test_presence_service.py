#!/usr/bin/env python3
"""Unit and local-socket tests for presenced and presence-run."""

import json
import os
import socket
import stat
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOLS = os.path.join(ROOT, "tools")
PRESENCE_HOST = os.path.join(ROOT, "host", "presence")
sys.path.insert(0, TOOLS)
sys.path.insert(0, PRESENCE_HOST)
sys.path.insert(0, HERE)

import presence_client as pc  # noqa: E402
import presence_service as ps  # noqa: E402
import presence_verify as pv  # noqa: E402
import test_presence_verify as tpv  # noqa: E402

# An AF_UNIX address carries the whole path in a fixed 104-byte field on macOS
# (108 on Linux), and tempfile follows TMPDIR. The isolated verify sweep points
# TMPDIR deep inside a copied candidate tree, where "<tmpdir>/presenced.sock" no
# longer fits and every socket test dies in setUp with "AF_UNIX path too long".
# Nothing about the product is at fault -- presenced defaults to
# ~/.openaliro/presenced.sock -- so this belongs in the test, not in the server.
SUN_PATH_MAX = 104
SOCKET_NAME = "presenced.sock"


def short_socket_dir():
    """A TemporaryDirectory whose socket path fits an AF_UNIX address.

    TMPDIR is honoured whenever it fits, because a sandbox that sets it means
    it. The fallback is only for the case where honouring it cannot work.
    """
    for base in (None, "/tmp"):
        try:
            directory = tempfile.TemporaryDirectory(dir=base)
        except OSError:
            continue
        if len(os.path.join(directory.name, SOCKET_NAME).encode()) < SUN_PATH_MAX:
            return directory
        directory.cleanup()
    raise unittest.SkipTest("no temp directory short enough for an AF_UNIX socket path")


class EnrollmentTests(unittest.TestCase):
    def write_store(self, directory, entries):
        path = os.path.join(directory, "enrolled")
        with open(path, "w", encoding="utf-8") as fh:
            for name, point, cred_id in entries:
                fh.write(f"{name} {point.hex()} {cred_id.hex()}\n")
        return path

    def test_single_enrollment_is_selected_and_pins_both_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_store(
                directory, [("test-device", tpv.KAT_POINT, tpv.KAT_CREDID)]
            )
            enrolled = ps.select_enrollment(path)
        self.assertEqual(enrolled.point, tpv.KAT_POINT)
        self.assertEqual(enrolled.cred_id, tpv.KAT_CREDID)
        self.assertEqual(enrolled.key_id, ps.pg.key_id(tpv.KAT_POINT).hex())

    def test_multiple_enrollments_require_an_explicit_key_id(self):
        other_point = b"\x04" + b"\x11" * 64
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_store(
                directory,
                [
                    ("first", tpv.KAT_POINT, tpv.KAT_CREDID),
                    ("second", other_point, b"\x22" * pv.CREDID_LEN),
                ],
            )
            with self.assertRaises(ps.ServiceError) as cm:
                ps.select_enrollment(path)
        self.assertIn("--key-id", str(cm.exception))

    def test_explicit_key_id_selects_one_enrollment(self):
        other_point = b"\x04" + b"\x11" * 64
        wanted = ps.pg.key_id(other_point).hex()
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_store(
                directory,
                [
                    ("first", tpv.KAT_POINT, tpv.KAT_CREDID),
                    ("second", other_point, b"\x22" * pv.CREDID_LEN),
                ],
            )
            enrolled = ps.select_enrollment(path, wanted)
        self.assertEqual(enrolled.key_id, wanted)

    def test_local_enrollment_is_owner_only_and_round_trips(self):
        port = FakePort()
        with tempfile.TemporaryDirectory() as directory:
            os.chmod(directory, 0o700)
            path = os.path.join(directory, "enrolled")
            with (
                mock.patch.object(ps.pg, "open_port", return_value=port),
                mock.patch.object(ps.pg, "dongle_pubkey", return_value=tpv.KAT_POINT),
                mock.patch.object(
                    ps.pg, "dongle_credential", return_value=tpv.KAT_CREDID
                ),
            ):
                written = ps.enroll_device("/dev/test", path=path)
            loaded = ps.select_enrollment(path)
            mode = stat.S_IMODE(os.stat(path).st_mode)
        self.assertTrue(port.closed)
        self.assertEqual(mode, 0o600)
        self.assertEqual(loaded, written)

    def test_local_enrollment_refuses_implicit_trust_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            os.chmod(directory, 0o700)
            path = self.write_store(
                directory, [("old-device", tpv.KAT_POINT, tpv.KAT_CREDID)]
            )
            os.chmod(path, 0o600)
            with (
                mock.patch.object(ps.pg, "open_port") as open_port,
                self.assertRaises(ps.ServiceError) as cm,
            ):
                ps.enroll_device("/dev/test", path=path)
        self.assertIn("--replace", str(cm.exception))
        open_port.assert_not_called()


class FakePort:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True


class DevicePinTests(unittest.TestCase):
    def enrollment(self):
        return ps.Enrollment(
            key_id=ps.pg.key_id(tpv.KAT_POINT).hex(),
            name="test-device",
            point=tpv.KAT_POINT,
            cred_id=tpv.KAT_CREDID,
        )

    def test_connected_device_must_match_enrolled_key_and_credential(self):
        port = FakePort()
        with (
            mock.patch.object(ps.pg, "open_port", return_value=port),
            mock.patch.object(ps.pg, "dongle_pubkey", return_value=tpv.KAT_POINT),
            mock.patch.object(ps.pg, "dongle_credential", return_value=tpv.KAT_CREDID),
        ):
            engine = ps.connect_engine("/dev/test", self.enrollment())
        self.assertIs(engine.serial, port)
        self.assertFalse(port.closed)

    def test_wrong_device_key_fails_closed_and_closes_serial(self):
        port = FakePort()
        with (
            mock.patch.object(ps.pg, "open_port", return_value=port),
            mock.patch.object(
                ps.pg, "dongle_pubkey", return_value=b"\x04" + b"\x33" * 64
            ),
            mock.patch.object(ps.pg, "dongle_credential", return_value=tpv.KAT_CREDID),
            self.assertRaises(ps.ServiceError),
        ):
            ps.connect_engine("/dev/test", self.enrollment())
        self.assertTrue(port.closed)

    def test_wrong_credential_fails_closed_and_closes_serial(self):
        port = FakePort()
        with (
            mock.patch.object(ps.pg, "open_port", return_value=port),
            mock.patch.object(ps.pg, "dongle_pubkey", return_value=tpv.KAT_POINT),
            mock.patch.object(
                ps.pg, "dongle_credential", return_value=b"\x44" * pv.CREDID_LEN
            ),
            self.assertRaises(ps.ServiceError),
        ):
            ps.connect_engine("/dev/test", self.enrollment())
        self.assertTrue(port.closed)


class EngineTests(unittest.TestCase):
    def engine(self, point=tpv.KAT_POINT, cred_id=tpv.KAT_CREDID):
        return ps.PresenceEngine(FakePort(), point, cred_id)

    def run_kat(self, engine, nonce=tpv.KAT_NONCE, max_cm=40):
        with (
            mock.patch.object(ps.os, "urandom", return_value=nonce),
            mock.patch.object(ps.pg, "dongle_prove", return_value=tpv.KAT_FRAME),
        ):
            return engine.prove(max_cm)

    def test_real_signature_success(self):
        result = self.run_kat(self.engine())
        self.assertTrue(result["ok"])
        self.assertEqual(result["verdict"], "OK")
        self.assertEqual(result["distance_cm"], 25)
        self.assertNotIn("cred_id", result)
        self.assertNotIn("nonce", result)
        self.assertNotIn("frame", result)

    def test_replayed_nonce_is_rejected(self):
        result = self.run_kat(self.engine(), nonce=b"\x99" * pv.NONCE_LEN)
        self.assertFalse(result["ok"])
        self.assertEqual(result["code"], "E_NONCE")
        self.assertEqual(result["verdict"], "E_NONCE")

    def test_wrong_credential_is_rejected(self):
        result = self.run_kat(self.engine(cred_id=b"\x55" * pv.CREDID_LEN))
        self.assertFalse(result["ok"])
        self.assertEqual(result["verdict"], "E_CREDENTIAL")

    def test_wrong_signing_key_is_rejected(self):
        result = self.run_kat(self.engine(point=b"\x04" + b"\x66" * 64))
        self.assertFalse(result["ok"])
        self.assertEqual(result["verdict"], "E_MAC")

    def test_excessive_range_is_rejected(self):
        result = self.run_kat(self.engine(), max_cm=24)
        self.assertFalse(result["ok"])
        self.assertEqual(result["verdict"], "E_RANGE")

    def test_firmware_timeout_is_safe_and_does_not_return_the_nonce(self):
        with (
            mock.patch.object(ps.os, "urandom", return_value=tpv.KAT_NONCE),
            mock.patch.object(
                ps.pg,
                "dongle_prove",
                side_effect=ps.pg.PresenceError(
                    "dongle refused 'presence prove deadbeef': proof timed out"
                ),
            ),
        ):
            result = self.engine().prove(40)
        self.assertFalse(result["ok"])
        self.assertEqual(result["code"], "TRANSPORT")
        self.assertNotIn("deadbeef", result["reason"])
        self.assertNotIn("presence prove", result["reason"])

    def test_concurrent_proofs_use_distinct_nonces_and_never_overlap_serial(self):
        engine = self.engine()
        active = 0
        peak = 0
        nonces = []
        counter = 0
        test_lock = threading.Lock()

        def fake_urandom(size):
            nonlocal counter
            counter += 1
            return counter.to_bytes(size, "big")

        def fake_prove(_serial, nonce):
            nonlocal active, peak
            with test_lock:
                active += 1
                peak = max(peak, active)
                nonces.append(nonce)
            time.sleep(0.02)
            with test_lock:
                active -= 1
            return nonce

        def fake_verify(frame, _point, nonce, _cred_id, **_kwargs):
            self.assertEqual(frame, nonce)
            return pv.OK, {
                "distance_cm": 5,
                "uptime_ms": 1,
                "cred_id": tpv.KAT_CREDID,
            }

        results = []
        with (
            mock.patch.object(ps.os, "urandom", side_effect=fake_urandom),
            mock.patch.object(ps.pg, "dongle_prove", side_effect=fake_prove),
            mock.patch.object(ps.pv, "verify", side_effect=fake_verify),
        ):
            threads = [
                threading.Thread(target=lambda: results.append(engine.prove(40)))
                for _ in range(2)
            ]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()

        self.assertEqual(peak, 1)
        self.assertEqual(len(set(nonces)), 2)
        self.assertEqual([result["ok"] for result in results], [True, True])


class StubEngine:
    def __init__(self, result=None):
        self.result = result or {
            "ok": True,
            "verdict": "OK",
            "reason": "presence confirmed",
            "distance_cm": 8,
        }
        self.calls = []

    def prove(self, max_cm):
        self.calls.append(max_cm)
        return dict(self.result)


class PolicyTests(unittest.TestCase):
    def setUp(self):
        self.engine = StubEngine()
        self.service = ps.PresenceService(self.engine, max_cm=40)

    def test_valid_request_uses_requested_stricter_limit(self):
        response = self.service.handle({"op": "prove", "max_cm": 25})
        self.assertTrue(response["ok"])
        self.assertEqual(self.engine.calls, [25])

    def test_request_cannot_weaken_daemon_distance_policy(self):
        response = self.service.handle({"op": "prove", "max_cm": 41})
        self.assertFalse(response["ok"])
        self.assertEqual(response["code"], "POLICY")
        self.assertEqual(self.engine.calls, [])

    def test_zero_boolean_and_non_integer_limits_are_rejected_without_proof(self):
        for value in (0, True, "40"):
            with self.subTest(value=value):
                response = self.service.handle({"op": "prove", "max_cm": value})
                self.assertFalse(response["ok"])
        self.assertEqual(self.engine.calls, [])

    def test_unknown_operation_is_rejected_without_proof(self):
        response = self.service.handle({"op": "status", "max_cm": 40})
        self.assertFalse(response["ok"])
        self.assertEqual(response["code"], "BAD_REQUEST")
        self.assertEqual(self.engine.calls, [])


class SocketTests(unittest.TestCase):
    def setUp(self):
        self.directory = short_socket_dir()
        os.chmod(self.directory.name, 0o700)
        self.path = os.path.join(self.directory.name, SOCKET_NAME)
        self.engine = StubEngine()
        self.service = ps.PresenceService(self.engine, max_cm=40)
        self.server = ps.PresenceUnixServer(self.path, self.service, request_timeout=1)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.close_and_unlink()
        self.directory.cleanup()

    def test_socket_is_owner_only_and_client_round_trip_succeeds(self):
        self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode), 0o600)
        response = pc.request_presence(self.path, max_cm=20, timeout=1)
        self.assertTrue(response["ok"])
        self.assertEqual(self.engine.calls, [20])

    def test_verified_denial_round_trip_remains_a_denial(self):
        self.engine.result = {
            "ok": False,
            "code": "E_RANGE",
            "verdict": "E_RANGE",
            "reason": "outside threshold",
        }
        response = pc.request_presence(self.path, max_cm=20, timeout=1)
        self.assertFalse(response["ok"])
        self.assertEqual(response["code"], "E_RANGE")

    def test_malformed_json_fails_closed(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.connect(self.path)
            client.sendall(b"not json\n")
            response = json.loads(client.makefile("rb").readline())
        self.assertFalse(response["ok"])
        self.assertEqual(response["code"], "BAD_REQUEST")
        self.assertEqual(self.engine.calls, [])

    def test_oversized_request_fails_closed(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.connect(self.path)
            client.sendall(b"{" + b"x" * ps.MAX_REQUEST_BYTES + b"}\n")
            response = json.loads(client.makefile("rb").readline())
        self.assertFalse(response["ok"])
        self.assertEqual(response["code"], "BAD_REQUEST")
        self.assertEqual(self.engine.calls, [])


class CommandGateTests(unittest.TestCase):
    def test_denial_never_starts_command(self):
        runner = mock.Mock()
        with self.assertRaises(pc.PresenceDenied):
            pc.execute_command(
                {"ok": False, "code": "E_RANGE", "reason": "outside threshold"},
                ["echo", "must-not-run"],
                runner=runner,
            )
        runner.assert_not_called()

    def test_success_executes_exact_argv_without_a_shell_and_preserves_exit(self):
        runner = mock.Mock(return_value=mock.Mock(returncode=23))
        rc = pc.execute_command(
            {"ok": True, "verdict": "OK", "distance_cm": 8},
            ["printf", "%s", "safe"],
            runner=runner,
        )
        self.assertEqual(rc, 23)
        runner.assert_called_once_with(["printf", "%s", "safe"], shell=False)

    def test_signal_exit_uses_shell_convention(self):
        runner = mock.Mock(return_value=mock.Mock(returncode=-9))
        rc = pc.execute_command(
            {"ok": True, "verdict": "OK", "distance_cm": 8},
            ["test-command"],
            runner=runner,
        )
        self.assertEqual(rc, 137)

    def test_malformed_success_never_starts_command(self):
        runner = mock.Mock()
        with self.assertRaises(pc.PresenceClientError):
            pc.execute_command({"ok": True}, ["echo", "must-not-run"], runner=runner)
        runner.assert_not_called()


if __name__ == "__main__":
    unittest.main()
