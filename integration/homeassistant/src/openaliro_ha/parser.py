"""Narrow parser for the currently verified nRF5340 console output."""

import re
from typing import Optional

from .models import AccessEvent, DistanceReading, Observation


ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
RANGE_RE = re.compile(r"rng\s+blk=(\d+)\s+d=(-?\d+)mm\s+tof=(-?\d+)")
DIST_RE = re.compile(r"DIST tof=(-?\d+) d=(-?\d+)mm")
ACCESS_RE = re.compile(r"ACCESS (GRANTED|DENIED)")


def strip_ansi(line: str) -> str:
    """Remove ANSI control sequences without otherwise normalizing console text."""

    return ANSI_ESCAPE_RE.sub("", line)


def parse_console_line(line: str) -> Optional[Observation]:
    """Convert one verified streaming range or access line into an observation.

    The curated ``rng`` line is preferred, but it exists only under
    ``CONFIG_WOZ_PRETTY_SHELL`` with ``aliro frames`` on, so the always-present
    ``DIST`` diagnostic is used as a fallback. Its ``phone_d`` field is the
    peer's own estimate and is deliberately not represented. Unknown text is
    ignored, so no credential identifiers reach an observation.
    """

    text = strip_ansi(line)
    match = RANGE_RE.search(text)
    if match:
        return DistanceReading(
            block=int(match.group(1)),
            distance_mm=int(match.group(2)),
            tof=int(match.group(3)),
        )

    match = DIST_RE.search(text)
    if match:
        return DistanceReading(block=None, distance_mm=int(match.group(2)), tof=int(match.group(1)))

    match = ACCESS_RE.search(text)
    if match:
        return AccessEvent(verdict=match.group(1).lower())

    return None
