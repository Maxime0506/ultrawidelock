"""Small, non-interactive HA=1 staging CLI for safe offline operations."""

import argparse
import json
import os
from dataclasses import asdict
from pathlib import Path
from typing import Sequence

from . import __version__
from .models import AccessEvent, DistanceReading, Observation
from .parser import parse_console_line


CONSOLE_SCHEMA = "streaming-v1"
MAX_REPLAY_BYTES = 1024 * 1024


def _observation_dict(observation: Observation) -> dict[str, object]:
    if isinstance(observation, DistanceReading):
        return {"kind": "range", **asdict(observation)}
    if isinstance(observation, AccessEvent):
        return {"kind": "access", **asdict(observation)}
    raise TypeError("unsupported observation")


def _read_capture(path: Path) -> list[Observation]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError("capture cannot be read") from error
    if len(data) > MAX_REPLAY_BYTES:
        raise ValueError("capture exceeds the size limit")
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("capture is not valid UTF-8") from error
    return [observation for line in lines if (observation := parse_console_line(line))]


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="openaliro-ha")
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("version", help="print agent and supported console-schema versions")
    replay = subcommands.add_parser(
        "replay",
        help="parse a sanitized capture without a board or broker",
    )
    replay.add_argument("capture", type=Path)
    replay.add_argument("--json", action="store_true", help="emit a stable JSON array")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Run an HA=1-gated offline command without exposing raw capture text."""

    parser = _parser()
    arguments = parser.parse_args(argv)
    if os.environ.get("HA") != "1":
        parser.error("openaliro-ha staging commands require HA=1")
    if arguments.command == "version":
        print(f"openaliro-ha {__version__} (console schema: {CONSOLE_SCHEMA})")
        return 0
    try:
        observations = _read_capture(arguments.capture)
    except ValueError as error:
        parser.error(str(error))
    rendered = [_observation_dict(observation) for observation in observations]
    if arguments.json:
        print(json.dumps(rendered, sort_keys=True))
    else:
        for observation in rendered:
            print(json.dumps(observation, sort_keys=True))
    return 0
