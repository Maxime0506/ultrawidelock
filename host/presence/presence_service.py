#!/usr/bin/env python3
"""Fresh, pinned presence proofs behind an owner-only Unix socket."""

from __future__ import annotations

import argparse
import errno
import json
import os
import signal
import socket
import socketserver
import stat
import sys
import tempfile
import threading
from dataclasses import dataclass

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(REPO_ROOT, "tools")
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import presence_git as pg  # noqa: E402
import presence_verify as pv  # noqa: E402

DEFAULT_SOCKET = os.path.join(os.path.expanduser("~"), ".openaliro", "presenced.sock")
DEFAULT_ENROLLED = os.path.join(os.path.expanduser("~"), ".openaliro", "enrolled")
MAX_REQUEST_BYTES = 1024
DEFAULT_REQUEST_TIMEOUT_S = 3


class ServiceError(RuntimeError):
    """Configuration or startup failure that keeps the daemon fail-closed."""


@dataclass(frozen=True)
class Enrollment:
    """One pinned device point and the only credential it may prove."""

    key_id: str
    name: str
    point: bytes
    cred_id: bytes


def select_enrollment(path: str, key_id: str | None = None) -> Enrollment:
    """Load exactly one enrollment, or select one explicitly by key id."""
    try:
        entries = pg.read_enrolled(path)
    except pg.PresenceError as exc:
        raise ServiceError(f"cannot load enrollment store: {exc}") from exc
    if not entries:
        raise ServiceError("enrollment store contains no trusted presence device")

    if key_id is None:
        if len(entries) != 1:
            raise ServiceError(
                "enrollment store contains multiple devices; select one with --key-id"
            )
        selected_id = next(iter(entries))
    else:
        selected_id = key_id.lower()
        if selected_id not in entries:
            raise ServiceError("--key-id does not name a device in the enrollment store")

    name, point, cred_id = entries[selected_id]
    return Enrollment(selected_id, name, point, cred_id)


def _secure_directory(path: str):
    os.makedirs(path, mode=0o700, exist_ok=True)
    directory_stat = os.stat(path)
    if directory_stat.st_uid != os.getuid():
        raise ServiceError("local presence directory is not owned by the current user")
    if stat.S_IMODE(directory_stat.st_mode) & 0o077:
        raise ServiceError(
            "local presence directory must not be accessible by group or others"
        )


def enroll_device(port: str, path: str = DEFAULT_ENROLLED, replace: bool = False):
    """Pin the attached device and credential into one owner-only local file."""
    parent = os.path.dirname(os.path.abspath(path))
    _secure_directory(parent)

    try:
        existing = os.lstat(path)
    except FileNotFoundError:
        existing = None
    if existing is not None:
        if (
            not stat.S_ISREG(existing.st_mode)
            or existing.st_uid != os.getuid()
            or stat.S_IMODE(existing.st_mode) & 0o077
        ):
            raise ServiceError("refusing to replace an insecure or unowned enrollment file")
        if not replace:
            raise ServiceError("local enrollment already exists; use --replace to change trust")

    serial = pg.open_port(port)
    try:
        point = pg.dongle_pubkey(serial)
        cred_id = pg.dongle_credential(serial)
    finally:
        serial.close()

    key_id = pg.key_id(point).hex()
    fd, temporary = tempfile.mkstemp(prefix=".enrolled.", dir=parent, text=True)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write("# Local presenced trust: <name> <device-point> <credential-id>\n")
            fh.write(f"primary {point.hex()} {cred_id.hex()}\n")
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(temporary, path)
        os.chmod(path, 0o600)
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise
    return Enrollment(key_id, "primary", point, cred_id)


class PresenceEngine:
    """Own the serial device and serialize fresh challenge/proof transactions."""

    def __init__(self, serial, point: bytes, cred_id: bytes, openssl: str = "openssl"):
        self.serial = serial
        self.point = point
        self.cred_id = cred_id
        self.openssl = openssl
        self._proof_lock = threading.Lock()

    def close(self):
        self.serial.close()

    def prove(self, max_cm: int) -> dict:
        """Mint one nonce, acquire one fresh proof, and return a safe verdict."""
        with self._proof_lock:
            nonce = os.urandom(pv.NONCE_LEN)
            try:
                frame = pg.dongle_prove(self.serial, nonce)
                verdict, fields = pv.verify(
                    frame,
                    self.point,
                    nonce,
                    self.cred_id,
                    max_cm=max_cm,
                    openssl=self.openssl,
                )
            except pg.PresenceError as exc:
                reason = str(exc).lower()
                if "timed out" in reason:
                    message = "proof timed out; wake the phone and hold it near the reader"
                else:
                    message = "device did not return a usable fresh proof"
                return {"ok": False, "code": "TRANSPORT", "reason": message}
            except pv.OpensslMissing:
                return {
                    "ok": False,
                    "code": "VERIFY_UNAVAILABLE",
                    "reason": "signature verification is unavailable",
                }

        result = {
            "ok": verdict == pv.OK,
            "code": pv.VERDICT_NAME[verdict],
            "verdict": pv.VERDICT_NAME[verdict],
            "reason": pv.VERDICT_REASON[verdict],
        }
        if verdict == pv.OK:
            result["distance_cm"] = fields["distance_cm"]
        return result


def connect_engine(
    port: str,
    enrolled: Enrollment,
    openssl: str = "openssl",
) -> PresenceEngine:
    """Open the device once and pin both identities before serving requests."""
    serial = pg.open_port(port)
    try:
        point = pg.dongle_pubkey(serial)
        cred_id = pg.dongle_credential(serial)
        if point != enrolled.point:
            raise ServiceError("connected device signing key is not the selected enrollment")
        if cred_id != enrolled.cred_id:
            raise ServiceError("connected device credential is not the selected enrollment")
        return PresenceEngine(serial, enrolled.point, enrolled.cred_id, openssl=openssl)
    except Exception:
        serial.close()
        raise


class PresenceService:
    """Validate socket requests before allowing them to touch the serial device."""

    def __init__(self, engine: PresenceEngine, max_cm: int = 40):
        if isinstance(max_cm, bool) or not isinstance(max_cm, int) or max_cm < 1:
            raise ServiceError("daemon maximum distance must be a positive integer")
        self.engine = engine
        self.max_cm = max_cm

    def handle(self, request) -> dict:
        if not isinstance(request, dict) or set(request) != {"op", "max_cm"}:
            return {
                "ok": False,
                "code": "BAD_REQUEST",
                "reason": "request must contain only op and max_cm",
            }
        if request["op"] != "prove":
            return {"ok": False, "code": "BAD_REQUEST", "reason": "unknown operation"}

        requested = request["max_cm"]
        if (
            isinstance(requested, bool)
            or not isinstance(requested, int)
            or requested < 1
        ):
            return {
                "ok": False,
                "code": "BAD_REQUEST",
                "reason": "max_cm must be a positive integer",
            }
        if requested > self.max_cm:
            return {
                "ok": False,
                "code": "POLICY",
                "reason": "requested distance exceeds daemon policy",
            }
        return self.engine.prove(requested)


class _RequestHandler(socketserver.StreamRequestHandler):
    def _send(self, response):
        encoded = json.dumps(response, separators=(",", ":"), sort_keys=True).encode()
        try:
            self.wfile.write(encoded + b"\n")
        except (BrokenPipeError, ConnectionResetError):
            pass

    def handle(self):
        self.request.settimeout(self.server.request_timeout)
        try:
            raw = self.rfile.readline(MAX_REQUEST_BYTES + 1)
        except (OSError, TimeoutError):
            self._send(
                {"ok": False, "code": "BAD_REQUEST", "reason": "request timed out"}
            )
            return
        if not raw or len(raw) > MAX_REQUEST_BYTES or not raw.endswith(b"\n"):
            self._send(
                {
                    "ok": False,
                    "code": "BAD_REQUEST",
                    "reason": "request is empty, incomplete, or too large",
                }
            )
            return
        try:
            request = json.loads(raw)
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._send(
                {"ok": False, "code": "BAD_REQUEST", "reason": "request is not JSON"}
            )
            return
        try:
            response = self.server.service.handle(request)
        except Exception:
            response = {
                "ok": False,
                "code": "INTERNAL",
                "reason": "presence service failed closed",
            }
        self._send(response)


def _prepare_socket_path(path: str):
    parent = os.path.dirname(os.path.abspath(path))
    _secure_directory(parent)

    try:
        existing = os.lstat(path)
    except FileNotFoundError:
        return
    if not stat.S_ISSOCK(existing.st_mode) or existing.st_uid != os.getuid():
        raise ServiceError("refusing to replace a non-socket or unowned socket path")

    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    probe.settimeout(0.2)
    try:
        probe.connect(path)
    except OSError as exc:
        if exc.errno not in (errno.ECONNREFUSED, errno.ENOENT):
            raise ServiceError("cannot determine whether an existing socket is active") from exc
    else:
        raise ServiceError("another presenced process is already listening")
    finally:
        probe.close()

    current = os.lstat(path)
    if (current.st_dev, current.st_ino) != (existing.st_dev, existing.st_ino):
        raise ServiceError("socket path changed while checking stale state")
    os.unlink(path)


class PresenceUnixServer(socketserver.UnixStreamServer):
    """Single-worker Unix server; every serial proof is inherently serialized."""

    def __init__(
        self,
        path: str,
        service: PresenceService,
        request_timeout: float = DEFAULT_REQUEST_TIMEOUT_S,
    ):
        _prepare_socket_path(path)
        self.service = service
        self.request_timeout = request_timeout
        self.socket_path = path
        super().__init__(path, _RequestHandler)
        os.chmod(path, 0o600)
        created = os.lstat(path)
        self._socket_identity = (created.st_dev, created.st_ino)

    def close_and_unlink(self):
        self.server_close()
        try:
            current = os.lstat(self.socket_path)
        except FileNotFoundError:
            return
        if (
            stat.S_ISSOCK(current.st_mode)
            and (current.st_dev, current.st_ino) == self._socket_identity
        ):
            os.unlink(self.socket_path)


def positive_cm(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 1 or parsed > 65534:
        raise argparse.ArgumentTypeError("must be between 1 and 65534")
    return parsed


def build_daemon_parser():
    parser = argparse.ArgumentParser(
        description="Serve fresh, pinned Aliro presence proofs over a Unix socket."
    )
    parser.add_argument("--port", required=True, help="ESP32 serial console")
    parser.add_argument("--socket", default=DEFAULT_SOCKET, help="owner-only Unix socket")
    parser.add_argument(
        "--enrolled",
        default=DEFAULT_ENROLLED,
        help="trusted device and credential enrollment file",
    )
    parser.add_argument(
        "--key-id",
        help="required when the enrollment file contains more than one device",
    )
    parser.add_argument(
        "--max-cm",
        type=positive_cm,
        default=40,
        help="maximum client-selectable distance threshold (default: 40)",
    )
    parser.add_argument("--openssl", default="openssl")
    return parser


def build_enroll_parser():
    parser = argparse.ArgumentParser(
        prog="presence-enroll",
        description="Pin the attached device and credential for local presenced use.",
    )
    parser.add_argument("--port", required=True, help="ESP32 serial console")
    parser.add_argument(
        "--output",
        default=DEFAULT_ENROLLED,
        help="owner-only local enrollment file",
    )
    parser.add_argument(
        "--replace",
        action="store_true",
        help="explicitly replace an existing local trust enrollment",
    )
    return parser


def enroll_main(argv=None) -> int:
    args = build_enroll_parser().parse_args(argv)
    try:
        enroll_device(args.port, path=args.output, replace=args.replace)
    except (ServiceError, pg.PresenceError, OSError) as exc:
        print(f"presence-enroll: {exc}", file=sys.stderr)
        return 1
    print("presence-enroll: saved one pinned device and credential", flush=True)
    return 0


def daemon_main(argv=None) -> int:
    args = build_daemon_parser().parse_args(argv)
    engine = None
    server = None
    old_handlers = {}
    try:
        enrolled = select_enrollment(args.enrolled, args.key_id)
        engine = connect_engine(args.port, enrolled, openssl=args.openssl)
        service = PresenceService(engine, max_cm=args.max_cm)
        server = PresenceUnixServer(args.socket, service)

        def stop(_signum, _frame):
            raise KeyboardInterrupt

        for signum in (signal.SIGINT, signal.SIGTERM):
            old_handlers[signum] = signal.signal(signum, stop)

        print("presenced: ready", flush=True)
        try:
            server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt:
            return 0
    except (ServiceError, pg.PresenceError, OSError) as exc:
        print(f"presenced: {exc}", file=sys.stderr)
        return 1
    finally:
        for signum, handler in old_handlers.items():
            signal.signal(signum, handler)
        if server is not None:
            server.close_and_unlink()
        if engine is not None:
            engine.close()
    return 0
