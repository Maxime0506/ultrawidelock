#!/usr/bin/env python3
"""HA=1-only tests for safe offline staging CLI commands."""

import contextlib
import io
import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.cli import main  # noqa: E402


CAPTURE = (
    ROOT
    / "integration"
    / "homeassistant"
    / "stage0"
    / "captures"
    / "synthetic_streaming_access.log"
)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class CliTests(unittest.TestCase):
    def invoke(self, arguments):
        stdout = io.StringIO()
        with patch.dict(os.environ, {"HA": "1"}, clear=False):
            with contextlib.redirect_stdout(stdout):
                exit_code = main(arguments)
        return exit_code, stdout.getvalue().splitlines()

    def test_version_prints_agent_and_schema(self):
        exit_code, lines = self.invoke(["version"])
        self.assertEqual(exit_code, 0)
        self.assertEqual(lines, ["openaliro-ha 0.0.0 (console schema: streaming-v1)"])

    def test_replay_json_emits_only_typed_observations(self):
        exit_code, lines = self.invoke(["replay", str(CAPTURE), "--json"])
        self.assertEqual(exit_code, 0)
        self.assertEqual(
            lines,
            [
                "[{\"block\": 7, \"distance_mm\": 1234, \"kind\": \"range\", \"tof\": 567}, "
                "{\"kind\": \"access\", \"verdict\": \"granted\"}, "
                "{\"kind\": \"access\", \"verdict\": \"denied\"}]"
            ],
        )

    def test_cli_rejects_missing_ha_gate(self):
        with patch.dict(os.environ, {}, clear=True):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    main(["version"])
        self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
