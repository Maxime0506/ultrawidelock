#!/usr/bin/env python3
"""HA=1-only tests for the source-proven ``aliro range`` response parser."""

import os
import sys
import unittest
from dataclasses import asdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.compatibility import RangeResponseParser  # noqa: E402
from openaliro_ha.models import CompatibilityRangeReading  # noqa: E402


SHELL_SOURCE = ROOT / "modules" / "woz_uwb" / "src" / "shell" / "aliro_shell.c"


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class CompatibilityRangeTests(unittest.TestCase):
    def test_shell_source_still_contains_the_compatibility_fields(self):
        source = SHELL_SOURCE.read_text(encoding="utf-8")
        for fragment in ("distance ", "nlos     ", "block    ", "age      ", "trusted  "):
            self.assertIn(fragment, source)

    def test_complete_ansi_response_emits_centimetre_precision_reading(self):
        parser = RangeResponseParser()
        parser.begin()
        lines = (
            "\x1b[2mdistance \x1b[0m\x1b[1m\x1b[36m87 cm\x1b[0m",
            "nlos     yes",
            "block    7",
            "age      250 ms",
            "trusted  no \u25cb",
        )
        for line in lines[:-1]:
            self.assertIsNone(parser.feed_line(line))
        self.assertEqual(
            parser.feed_line(lines[-1]),
            CompatibilityRangeReading(
                block=7,
                distance_mm=870,
                age_ms=250,
                trusted=False,
                nlos=True,
            ),
        )

    def test_peer_text_is_ignored_and_never_part_of_the_observation(self):
        parser = RangeResponseParser()
        parser.begin()
        for line in (
            "distance 142 cm",
            "peer     0xBEEF",
            "nlos     no",
            "block    9",
            "age      90 ms",
        ):
            self.assertIsNone(parser.feed_line(line))
        reading = parser.feed_line("trusted  yes \u25cf")
        self.assertEqual(
            reading,
            CompatibilityRangeReading(9, 1420, 90, trusted=True, nlos=False),
        )
        self.assertNotIn("peer", asdict(reading))

    def test_incomplete_negative_and_no_range_responses_emit_nothing(self):
        parser = RangeResponseParser()
        parser.begin()
        self.assertIsNone(parser.feed_line("trusted yes"))
        parser.begin()
        for line in ("distance -1 cm", "nlos no", "block 1", "age 1 ms"):
            self.assertIsNone(parser.feed_line(line))
        self.assertIsNone(parser.feed_line("trusted yes"))
        parser.begin()
        self.assertIsNone(parser.feed_line("no valid range since boot"))
        self.assertIsNone(parser.feed_line("distance 12 cm"))


if __name__ == "__main__":
    unittest.main(verbosity=1)
