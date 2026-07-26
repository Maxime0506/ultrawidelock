"""Incremental parser for the source-proven ``aliro range`` compatibility mode."""

import re
from typing import Optional

from .models import CompatibilityRangeReading
from .parser import strip_ansi


DISTANCE_RE = re.compile(r"\bdistance\s+(-?\d+)\s+cm\b")
NLOS_RE = re.compile(r"\bnlos\s+(yes|no)\b")
BLOCK_RE = re.compile(r"\bblock\s+(\d+)\b")
AGE_RE = re.compile(r"\bage\s+(-?\d+)\s+ms\b")
TRUSTED_RE = re.compile(r"\btrusted\s+(yes|no)\b")
NO_RANGE_RE = re.compile(r"\bno valid range since boot\b")


class RangeResponseParser:
    """Parse one explicitly started ``aliro range`` command response at a time."""

    def __init__(self) -> None:
        self._active = False
        self._distance_cm: Optional[int] = None
        self._nlos: Optional[bool] = None
        self._block: Optional[int] = None
        self._age_ms: Optional[int] = None

    def begin(self) -> None:
        """Start a new response, discarding any interrupted prior response."""

        self._active = True
        self._distance_cm = None
        self._nlos = None
        self._block = None
        self._age_ms = None

    def feed_line(self, line: str) -> Optional[CompatibilityRangeReading]:
        """Accept one response line and emit only a complete, internally valid reading."""

        if not self._active:
            return None
        text = strip_ansi(line)
        if NO_RANGE_RE.search(text):
            self._active = False
            return None
        if match := DISTANCE_RE.search(text):
            self._distance_cm = int(match.group(1))
            return None
        if match := NLOS_RE.search(text):
            self._nlos = match.group(1) == "yes"
            return None
        if match := BLOCK_RE.search(text):
            self._block = int(match.group(1))
            return None
        if match := AGE_RE.search(text):
            self._age_ms = int(match.group(1))
            return None
        if match := TRUSTED_RE.search(text):
            reading = self._complete_reading(trusted=match.group(1) == "yes")
            self._active = False
            return reading
        return None

    def _complete_reading(self, trusted: bool) -> Optional[CompatibilityRangeReading]:
        if None in (self._distance_cm, self._nlos, self._block, self._age_ms):
            return None
        if self._distance_cm < 0 or self._age_ms < 0:
            return None
        return CompatibilityRangeReading(
            block=self._block,
            distance_mm=self._distance_cm * 10,
            age_ms=self._age_ms,
            trusted=trusted,
            nlos=self._nlos,
        )
