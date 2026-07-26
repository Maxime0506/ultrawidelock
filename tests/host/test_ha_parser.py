#!/usr/bin/env python3
"""HA=1-only tests for the staged typed console parser."""

import os
import sys
import unittest
from dataclasses import asdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant"))
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

import aliro_mqtt_bridge as legacy_bridge  # noqa: E402
from openaliro_ha import AccessEvent, DistanceReading, parse_console_line, strip_ansi  # noqa: E402


PREFIX = "[00:01:02.345,678] <inf> woz_uwb: "


def legacy_shape(observation):
    """Express a typed observation in the legacy bridge's dictionary contract."""

    if isinstance(observation, DistanceReading):
        return {"kind": "range", **asdict(observation)}
    if isinstance(observation, AccessEvent):
        return {"kind": "access", **asdict(observation)}
    return None


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class ParserTests(unittest.TestCase):
    def test_streaming_range_preserves_current_values(self):
        self.assertEqual(
            parse_console_line(PREFIX + "rng  blk=7   d=-12mm  tof=-4"),
            DistanceReading(block=7, distance_mm=-12, tof=-4),
        )

    def test_ansi_is_ignored_before_matching(self):
        line = PREFIX + "\x1b[36mrng\x1b[0m  blk=123 d=88mm  tof=3"
        self.assertEqual(
            parse_console_line(line),
            DistanceReading(block=123, distance_mm=88, tof=3),
        )
        self.assertNotIn("\x1b", strip_ansi(line))

    def test_access_outcome_has_no_credential_identifier(self):
        event = parse_console_line(PREFIX + "ACCESS GRANTED for credential 0")
        self.assertEqual(event, AccessEvent(verdict="granted"))
        self.assertEqual(tuple(event.__dict__), ("verdict",))

    def test_unverified_and_malformed_lines_are_ignored(self):
        for line in (
            PREFIX + "DIST tof=3 d=88mm",
            PREFIX + "rng blk=x d=1mm tof=2",
            PREFIX + "access granted",
        ):
            self.assertIsNone(parse_console_line(line), line)

    def test_sanitized_fixture_matches_legacy_bridge_contract(self):
        fixture = (
            ROOT
            / "integration"
            / "homeassistant"
            / "stage0"
            / "captures"
            / "synthetic_streaming_access.log"
        )
        for line in fixture.read_text(encoding="utf-8").splitlines():
            self.assertEqual(
                legacy_shape(parse_console_line(line)),
                legacy_bridge.parse_line(line),
                line,
            )


if __name__ == "__main__":
    unittest.main(verbosity=1)
