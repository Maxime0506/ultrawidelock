#!/usr/bin/env python3
"""Client and command gate for the local presenced Unix socket."""

from __future__ import annotations

import argparse
import json
import os
import socket
import stat
import subprocess
import sys

from presence_service import DEFAULT_SOCKET

MAX_RESPONSE_BYTES = 4096


class PresenceClientError(RuntimeError):
    """Base exception for client-side presence proof errors (socket, validation, daemon rejection)."""
    pass


class PresenceDenied(PresenceClientError):
    """Exception raised when the presence daemon denies the proof request (proof failed, stale, or out of range)."""
    pass


def _validate_response(response) -> dict:
    """Validate a presenced response JSON object. Raises PresenceClientError if the response structure is invalid or contradictory (e.g., ok:true with missing/invalid distance, or ok:false with missing code/reason). Returns the validated response dict."""
    if not isinstance(response, dict) or type(response.get("ok")) is not bool:
        raise PresenceClientError("presenced returned an invalid response")
    if response["ok"]:
        if (
            response.get("verdict") != "OK"
            or isinstance(response.get("distance_cm"), bool)
            or not isinstance(response.get("distance_cm"), int)
            or response["distance_cm"] < 0
        ):
            raise PresenceClientError("presenced returned an invalid success response")
    elif not isinstance(response.get("code"), str) or not isinstance(
        response.get("reason"), str
    ):
        raise PresenceClientError("presenced returned an invalid denial response")
    return response


def _validate_socket(path: str):
    """Validate the presenced socket path: must exist, be a Unix socket, be owned by the current user, and have no group or other access. Raises PresenceClientError if any check fails."""
    try:
        socket_stat = os.lstat(path)
    except FileNotFoundError as exc:
        raise PresenceClientError("presenced socket does not exist") from exc
    if not stat.S_ISSOCK(socket_stat.st_mode):
        raise PresenceClientError("presenced path is not a Unix socket")
    if socket_stat.st_uid != os.getuid():
        raise PresenceClientError("presenced socket is not owned by the current user")
    if stat.S_IMODE(socket_stat.st_mode) & 0o077:
        raise PresenceClientError("presenced socket is accessible by group or others")


def request_presence(path: str, max_cm: int, timeout: float = 15.0) -> dict:
    """Request one fresh proof. The daemon, never the client, mints the nonce."""
    if isinstance(max_cm, bool) or not isinstance(max_cm, int) or max_cm < 1:
        raise PresenceClientError("max_cm must be a positive integer")
    if timeout <= 0:
        raise PresenceClientError("timeout must be positive")
    _validate_socket(path)

    request = json.dumps(
        {"op": "prove", "max_cm": max_cm},
        separators=(",", ":"),
        sort_keys=True,
    ).encode() + b"\n"

    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(timeout)
            client.connect(path)
            client.sendall(request)
            with client.makefile("rb") as stream:
                response_raw = stream.readline(MAX_RESPONSE_BYTES + 1)
    except (OSError, TimeoutError) as exc:
        raise PresenceClientError("could not complete a presenced request") from exc

    if (
        not response_raw
        or len(response_raw) > MAX_RESPONSE_BYTES
        or not response_raw.endswith(b"\n")
    ):
        raise PresenceClientError("presenced returned an empty, incomplete, or oversized response")
    try:
        response = json.loads(response_raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PresenceClientError("presenced returned invalid JSON") from exc
    return _validate_response(response)


def execute_command(response: dict, command: list[str], runner=subprocess.run) -> int:
    """Run exact argv only after success, preserving exit and signal status."""
    response = _validate_response(response)
    if response.get("ok") is not True:
        raise PresenceDenied(response.get("reason", "presence denied"))
    if not command:
        raise PresenceClientError("no command was provided")
    completed = runner(command, shell=False)
    if completed.returncode < 0:
        return 128 - completed.returncode
    return completed.returncode


def positive_cm(value: str) -> int:
    """Parse and validate a positive integer argument in cm for the distance threshold."""
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def positive_timeout(value: str) -> float:
    """Parse and validate a positive float argument in seconds for the socket and proof deadline."""
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def build_client_parser():
    """Build an argparse parser for the presence-run CLI: socket path, distance threshold in cm, timeout in seconds, and the command to run."""
    parser = argparse.ArgumentParser(
        prog="presence-run",
        description="Run exact argv only after presenced verifies fresh nearby presence.",
    )
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    parser.add_argument("--max-cm", type=positive_cm, default=40)
    parser.add_argument(
        "--timeout",
        type=positive_timeout,
        default=15.0,
        help="socket/proof deadline in seconds (default: 15)",
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    return parser


def client_main(argv=None) -> int:
    """Main entry point for presence-run CLI. Request a fresh proof from presenced, confirm it succeeded, print the distance, then execute the given command only if presence succeeded. Returns 0 on success, 1 on proof denial, 2 on unavailable socket/daemon, 126 on execution error, 127 on command not found."""
    parser = build_client_parser()
    args = parser.parse_args(argv)
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required after --")

    try:
        response = request_presence(args.socket, args.max_cm, timeout=args.timeout)
        if not response["ok"]:
            raise PresenceDenied(response.get("reason", "presence denied"))
        print(f"presence confirmed at {response['distance_cm']} cm", flush=True)
        return execute_command(response, command)
    except PresenceDenied as exc:
        print(f"presence-run: denied: {exc}", file=sys.stderr)
        return 1
    except PresenceClientError as exc:
        print(f"presence-run: unavailable: {exc}", file=sys.stderr)
        return 2
    except FileNotFoundError:
        print("presence-run: command not found", file=sys.stderr)
        return 127
    except OSError as exc:
        print(f"presence-run: could not execute command: {exc}", file=sys.stderr)
        return 126
