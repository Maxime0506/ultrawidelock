#!/usr/bin/env python3
"""HA=1-only tests for safe offline staging CLI commands."""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.cli import _configure, main  # noqa: E402
from openaliro_ha.serial_session import SessionState  # noqa: E402
from openaliro_ha.serial_transport import SerialPort  # noqa: E402


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

    def test_cli_commands_do_not_require_a_runtime_ha_environment(self):
        with patch.dict(os.environ, {}, clear=True):
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(main(["version"]), 0)
        self.assertIn("openaliro-ha", stdout.getvalue())

    def test_doctor_json_is_redacted_and_machine_readable(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            config_path = Path(temporary_directory) / "agent.toml"
            config_path.write_text(
                """schema_version = 1

[mqtt]
host = "broker.example.invalid"
allow_anonymous = true

[devices.front_door]
serial_port = "private-path"
""",
                encoding="utf-8",
            )
            stdout = io.StringIO()
            with patch.dict(os.environ, {"HA": "1"}, clear=False), patch(
                "openaliro_ha.cli.run_doctor",
                new=AsyncMock(return_value=(SimpleNamespace(device_id="front_door", serial_state="ready_streaming"),)),
            ), contextlib.redirect_stdout(stdout):
                exit_code = main(["--config", str(config_path), "doctor", "--json"])
        self.assertEqual(exit_code, 0)
        self.assertEqual(
            json.loads(stdout.getvalue()),
            {"devices": [{"device_id": "front_door", "serial_state": "ready_streaming"}], "ok": True},
        )

    def test_configure_probes_selected_port_and_writes_hashed_auto_identity(self):
        candidate = SerialPort(
            device="private-path",
            identity="0123456789abcdef01234567",
            vid=0x1366,
            pid=0x1051,
            interface="VCOM1",
            product="J-Link",
        )
        answers = iter(("1", "front-door", "broker.example.invalid", "agent", "MQTT_PASSWORD"))
        output = []
        with tempfile.TemporaryDirectory() as temporary_directory:
            config_path = Path(temporary_directory) / "agent.toml"
            arguments = SimpleNamespace(config=config_path)
            with patch("openaliro_ha.cli.discover_serial_ports", return_value=(candidate,)), patch(
                "openaliro_ha.cli.probe_device",
                new=AsyncMock(return_value=SessionState.READY_STREAMING),
            ):
                self.assertEqual(
                    _configure(arguments, input_fn=lambda _message: next(answers), output_fn=output.append),
                    0,
                )
            rendered = config_path.read_text(encoding="utf-8")
        self.assertIn('serial_port = "auto"', rendered)
        self.assertIn('serial_identity = "0123456789abcdef01234567"', rendered)
        self.assertNotIn("private-path", rendered)
        self.assertNotIn("broker.example.invalid", "\n".join(output))


if __name__ == "__main__":
    unittest.main(verbosity=1)
