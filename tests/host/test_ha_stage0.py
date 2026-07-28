#!/usr/bin/env python3
"""Validate the HA=1-gated Home Assistant Stage 0 evidence contract.

The test intentionally consumes only sanitized fixtures. It neither opens a
serial device nor connects to MQTT, and skips outside the explicit HA=1 path.
"""

import json
import os
import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STAGE0 = ROOT / "integration" / "homeassistant" / "stage0"
CAPTURES = STAGE0 / "captures"
sys.path.insert(0, str(ROOT / "integration" / "homeassistant"))

import aliro_mqtt_bridge as bridge  # noqa: E402


REQUIRED_HARDWARE_CASES = {
    "usb_identity",
    "boot_ansi_off",
    "boot_ansi_on",
    "version",
    "status",
    "frames",
    "range",
    "access",
    "malformed",
    "reconnect",
    "ha_runtime_matrix",
}

# Raw credential identifiers, peer identifiers, host paths, USB serial numbers,
# certificate blocks, and protocol-frame dumps are not permitted in fixtures.
FORBIDDEN_PATTERNS = (
    re.compile(r"(?i)credential\s+(?!<redacted>\b)\S+"),
    re.compile(r"(?i)\bpeer\s+0x[0-9a-f]+\b"),
    re.compile(r"/dev/"),
    re.compile(r"(?i)\b(?:usb|serial)\s*(?:number|no\.?|#)\s*[:=]"),
    re.compile(r"-----BEGIN (?:CERTIFICATE|.*PRIVATE KEY)-----"),
    re.compile(r"(?i)\b(?:raw\s+)?frame\s*[:=]\s*[0-9a-f]{16,}"),
)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class Stage0EvidenceTests(unittest.TestCase):
    def setUp(self):
        with (CAPTURES / "manifest.json").open(encoding="utf-8") as manifest_file:
            self.manifest = json.load(manifest_file)

    def test_manifest_schema_and_required_hardware_inventory(self):
        self.assertEqual(self.manifest["schema_version"], 1)
        cases = self.manifest["required_hardware_cases"]
        self.assertEqual({case["id"] for case in cases}, REQUIRED_HARDWARE_CASES)
        self.assertTrue(all(case["status"] in {"pending", "complete"} for case in cases))
        matrix = (STAGE0 / "hardware-matrix.md").read_text(encoding="utf-8")
        for installation_type in (
            "Home Assistant OS",
            "Home Assistant Container",
            "Home Assistant Core",
        ):
            self.assertIn(installation_type, matrix)

    def test_complete_capture_replays_to_expected_observations(self):
        complete_captures = [
            capture
            for capture in self.manifest["captures"]
            if capture["status"] == "complete"
        ]
        self.assertTrue(complete_captures, "at least one reviewed fixture is required")
        for capture in complete_captures:
            self.assertIn(capture["source"], {"synthetic", "hardware"})
            self.assertIn(capture["ansi"], {"on", "off", "mixed"})
            lines = (CAPTURES / capture["log"]).read_text(encoding="utf-8").splitlines()
            expected = json.loads((CAPTURES / capture["expected"]).read_text(encoding="utf-8"))
            observed = [reading for line in lines if (reading := bridge.parse_line(line))]
            self.assertEqual(observed, expected, capture["id"])

    def test_complete_capture_text_is_redacted(self):
        for capture in self.manifest["captures"]:
            if capture["status"] != "complete":
                continue
            text = (CAPTURES / capture["log"]).read_text(encoding="utf-8")
            for pattern in FORBIDDEN_PATTERNS:
                self.assertIsNone(pattern.search(text), f"{capture['id']}: {pattern.pattern}")


if __name__ == "__main__":
    unittest.main(verbosity=1)
