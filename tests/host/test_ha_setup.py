#!/usr/bin/env python3
"""HA=1-only checks on the one-step setup script.

The script's real work needs a broker, a board, and SSH, so none of it runs
here. What is checkable without any of that is its shape: that it parses, that
it fails fast, that every deployment-specific value stays overridable, and that
the credential never reaches a command line. Those are the properties that
broke or nearly broke in review, so they are the ones pinned.
"""

import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "integration" / "homeassistant" / "scripts" / "ha-setup.sh"


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class SetupScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SCRIPT.read_text(encoding="utf-8")

    def test_script_is_executable(self):
        self.assertTrue(SCRIPT.is_file(), SCRIPT)
        self.assertTrue(os.access(SCRIPT, os.X_OK), "ha-setup.sh must be executable")

    def test_script_parses(self):
        result = subprocess.run(
            ["bash", "-n", str(SCRIPT)], capture_output=True, text=True, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_script_fails_fast(self):
        self.assertIn("set -euo pipefail", self.text)

    def test_every_deployment_value_is_overridable(self):
        for variable in (
            "HA_SSH",
            "BROKER_HOST",
            "BROKER_PORT",
            "MQTT_USER",
            "DEVICE_ID",
            "CONFIG_DIR",
            "CERT_DAYS",
        ):
            self.assertIn(f'{variable}="${{{variable}:-', self.text, variable)

    def test_the_password_never_reaches_a_command_line(self):
        """The add-on options body goes over stdin, not argv.

        A password in argv is readable from any process listing on the Home
        Assistant box for as long as the request runs.
        """

        self.assertIn("-d @-", self.text)
        for line in self.text.splitlines():
            if "ssh" in line and "$MQTT_PASSWORD" in line:
                self.fail(f"password interpolated into an ssh command line: {line.strip()}")

    def test_the_password_file_is_created_privately(self):
        self.assertIn("umask 077", self.text)
        self.assertIn('chmod 600 "$PASSWORD_FILE"', self.text)

    def test_the_key_is_created_privately(self):
        self.assertIn('chmod 600 "$CERT" "$KEY"', self.text)

    def test_an_unreachable_broker_stops_the_run(self):
        """A wait loop that falls through leaves a later step to fail obscurely."""

        self.assertIn("broker_up=0", self.text)
        self.assertIn('if [ "$broker_up" = 0 ]; then', self.text)

    def test_an_existing_config_is_never_deleted(self):
        """configure merges, so a second device must survive a re-run."""

        self.assertNotIn('rm -f "$CONFIG"', self.text)
        self.assertIn('mv "$CONFIG" "$CONFIG.unreadable"', self.text)

    def test_certificate_carries_a_subject_alternative_name(self):
        """A certificate without a matching SAN fails verification on connect."""

        self.assertIn("subjectAltName=$san", self.text)
        self.assertIn('san="DNS:$BROKER_HOST"', self.text)

    def test_configure_is_driven_without_prompts(self):
        for flag in (
            "--device-id",
            "--mqtt-host",
            "--mqtt-port",
            "--mqtt-username",
            "--mqtt-password-file",
            "--mqtt-ca",
        ):
            self.assertIn(flag, self.text, flag)

    def test_run_ends_by_verifying_itself(self):
        self.assertIn("doctor", self.text)

    @unittest.skipUnless(shutil.which("shellcheck"), "shellcheck is not installed")
    def test_shellcheck_is_clean(self):
        result = subprocess.run(
            ["shellcheck", "--severity=warning", str(SCRIPT)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=1).result.wasSuccessful() else 1)
