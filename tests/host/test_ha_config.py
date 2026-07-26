#!/usr/bin/env python3
"""HA=1-only tests for versioned, secret-free agent configuration."""

import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.config import (  # noqa: E402
    AgentConfig,
    ConfigError,
    DeviceConfig,
    MqttConfig,
    load_config,
    redacted_config,
    serialize_config,
    write_config,
)


VALID_CONFIG = """\
schema_version = 1

[mqtt]
host = "broker.example.invalid"
port = 8883
tls = true
username = "agent"
password_env = "OPENALIRO_HA_MQTT_PASSWORD"

[devices.front_door]
serial_port = "auto"
baud = 115200
distance_mode = "auto"

[devices.side_door]
serial_port = "auto"
baud = 115200
distance_mode = "streaming"
"""


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class ConfigTests(unittest.TestCase):
    def write_text(self, directory: Path, text: str) -> Path:
        path = directory / "agent.toml"
        path.write_text(text, encoding="utf-8")
        return path

    def test_valid_multi_device_config_loads(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            config = load_config(self.write_text(Path(temporary_directory), VALID_CONFIG))
        self.assertEqual(config.schema_version, 1)
        self.assertEqual(config.mqtt.port, 8883)
        self.assertEqual(
            [device.device_id for device in config.devices],
            ["front_door", "side_door"],
        )

    def test_inline_password_is_rejected_without_echoing_it(self):
        raw_password = "not-a-real-secret"
        text = VALID_CONFIG + f"\n[mqtt.credentials]\npassword = \"{raw_password}\"\n"
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaises(ConfigError) as raised:
                load_config(self.write_text(Path(temporary_directory), text))
        self.assertNotIn(raw_password, str(raised.exception))

    def test_insecure_transport_requires_explicit_opt_in(self):
        text = VALID_CONFIG.replace("tls = true", "tls = false")
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(ConfigError, "allow_insecure"):
                load_config(self.write_text(Path(temporary_directory), text))

    def test_tls_client_certificate_requires_a_matching_key(self):
        text = VALID_CONFIG.replace(
            "password_env = \"OPENALIRO_HA_MQTT_PASSWORD\"",
            "password_env = \"OPENALIRO_HA_MQTT_PASSWORD\"\nclient_cert = \"client.pem\"",
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(ConfigError, "certificate and key"):
                load_config(self.write_text(Path(temporary_directory), text))

    def test_serialization_round_trip_and_permissions(self):
        config = AgentConfig(
            mqtt=MqttConfig(
                host="broker.example.invalid",
                username="agent",
                password_env="OPENALIRO_HA_MQTT_PASSWORD",
            ),
            devices=(DeviceConfig(device_id="front_door", serial_port="serial-board-a"),),
        )
        self.assertEqual(load_config_text(serialize_config(config)), config)
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "nested" / "agent.toml"
            write_config(path, config)
            self.assertEqual(load_config(path), config)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_diagnostics_are_redacted(self):
        config = AgentConfig(
            mqtt=MqttConfig(
                host="broker.example.invalid",
                username="agent",
                password_env="OPENALIRO_HA_MQTT_PASSWORD",
            ),
            devices=(DeviceConfig(device_id="front_door", serial_port="serial-board-a"),),
        )
        diagnostic = json.dumps(redacted_config(config))
        self.assertNotIn("broker.example.invalid", diagnostic)
        self.assertNotIn("OPENALIRO_HA_MQTT_PASSWORD", diagnostic)
        self.assertNotIn("serial-board-a", diagnostic)
        self.assertIn("password_source_configured", diagnostic)


def load_config_text(text: str) -> AgentConfig:
    with tempfile.TemporaryDirectory() as temporary_directory:
        path = Path(temporary_directory) / "agent.toml"
        path.write_text(text, encoding="utf-8")
        return load_config(path)


if __name__ == "__main__":
    unittest.main(verbosity=1)
