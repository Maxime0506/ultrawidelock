#!/usr/bin/env python3
"""HA=1-only tests for the standalone MQTT adapter using a fake client."""

import json
import os
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant"))
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

import aliro_mqtt_bridge as legacy_bridge  # noqa: E402
from openaliro_ha.config import MqttConfig  # noqa: E402
from openaliro_ha.models import AccessEvent, DistanceReading  # noqa: E402
from openaliro_ha.mqtt import MqttError, MqttPublisher, discovery_payloads  # noqa: E402


class FakeMqttClient:
    def __init__(self):
        self.calls = []

    def username_pw_set(self, username, password):
        self.calls.append(("username_pw_set", username, password))

    def tls_set(self, **kwargs):
        self.calls.append(("tls_set", kwargs))

    def tls_insecure_set(self, value):
        self.calls.append(("tls_insecure_set", value))

    def will_set(self, topic, payload, qos, retain):
        self.calls.append(("will_set", topic, payload, qos, retain))

    def connect(self, host, port, keepalive):
        self.calls.append(("connect", host, port, keepalive))

    def loop_start(self):
        self.calls.append(("loop_start",))
        self.on_connect(self, None, None, 0)

    def reconnect_delay_set(self, min_delay, max_delay):
        self.calls.append(("reconnect_delay_set", min_delay, max_delay))

    def loop_stop(self):
        self.calls.append(("loop_stop",))

    def disconnect(self):
        self.calls.append(("disconnect",))

    def publish(self, topic, payload, qos, retain):
        self.calls.append(("publish", topic, payload, qos, retain))


class FailingPublishMqttClient(FakeMqttClient):
    def publish(self, topic, payload, qos, retain):
        super().publish(topic, payload, qos, retain)
        raise RuntimeError("simulated broker failure")


class RejectedMqttClient(FakeMqttClient):
    def loop_start(self):
        self.calls.append(("loop_start",))
        self.on_connect(self, None, None, 4)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class MqttPublisherTests(unittest.TestCase):
    def publisher(self, config=None):
        client = FakeMqttClient()
        config = config or MqttConfig(
            host="broker.example.invalid",
            username="agent",
            password_env="OPENALIRO_HA_MQTT_PASSWORD",
        )
        publisher = MqttPublisher(
            config,
            "front_door",
            client_factory=lambda: client,
            environment={"OPENALIRO_HA_MQTT_PASSWORD": "not-a-real-secret"},
        )
        return publisher, client

    def test_discovery_contract_matches_the_legacy_bridge(self):
        self.assertEqual(
            discovery_payloads("front_door"),
            legacy_bridge.discovery_payloads("front_door"),
        )

    def test_start_uses_authenticated_tls_and_retained_discovery(self):
        publisher, client = self.publisher()
        publisher.start()
        self.assertEqual(client.calls[0], ("username_pw_set", "agent", "not-a-real-secret"))
        self.assertEqual(client.calls[1], ("tls_set", {}))
        self.assertEqual(client.calls[2], ("tls_insecure_set", False))
        self.assertEqual(client.calls[3], ("reconnect_delay_set", 1, 60))
        self.assertEqual(
            client.calls[4],
            ("will_set", "aliro/front_door/status", "offline", 1, True),
        )
        self.assertEqual(client.calls[5], ("connect", "broker.example.invalid", 8883, 60))
        publications = [call for call in client.calls if call[0] == "publish"]
        self.assertEqual(len(publications), 3)
        self.assertTrue(all(call[4] for call in publications))
        self.assertEqual(publications[-1][1:], ("aliro/front_door/status", "online", 1, True))

    def test_start_expands_a_user_relative_ca_path(self):
        config = MqttConfig(
            host="broker.example.invalid",
            username="agent",
            password_env="OPENALIRO_HA_MQTT_PASSWORD",
            ca_path="~/openaliro-ca.crt",
        )
        publisher, client = self.publisher(config)
        publisher.start()
        self.assertEqual(
            client.calls[1],
            ("tls_set", {"ca_certs": str(Path.home() / "openaliro-ca.crt")}),
        )

    def test_observations_are_non_retained_and_access_has_no_extra_fields(self):
        publisher, client = self.publisher()
        publisher.start()
        publisher.publish_distance(DistanceReading(block=7, distance_mm=1234, tof=567))
        publisher.publish_access(AccessEvent(verdict="denied"))
        publications = [call for call in client.calls if call[0] == "publish"]
        self.assertEqual(
            publications[-2],
            ("publish", "aliro/front_door/distance", "1234", 0, False),
        )
        self.assertEqual(publications[-1][1], "aliro/front_door/access")
        self.assertEqual(json.loads(publications[-1][2]), {"event_type": "denied"})
        self.assertFalse(publications[-1][4])

    def test_plaintext_requires_explicit_config_and_skips_tls_setup(self):
        config = MqttConfig(
            host="broker.example.invalid",
            tls=False,
            allow_insecure=True,
            allow_anonymous=True,
        )
        publisher, client = self.publisher(config)
        publisher.start()
        self.assertFalse(any(call[0] == "tls_set" for call in client.calls))

    def test_plaintext_without_opt_in_is_rejected_before_connecting(self):
        config = MqttConfig(host="broker.example.invalid", tls=False, allow_anonymous=True)
        with self.assertRaisesRegex(MqttError, "explicit insecure opt-in"):
            self.publisher(config)

    def test_missing_password_is_redacted(self):
        publisher, _ = self.publisher()
        publisher._environment = {}
        with self.assertRaises(MqttError) as raised:
            publisher.start()
        self.assertNotIn("OPENALIRO_HA_MQTT_PASSWORD", str(raised.exception))

    def test_close_publishes_retained_offline_before_disconnect(self):
        publisher, client = self.publisher()
        publisher.start()
        publisher.close()
        self.assertEqual(
            client.calls[-3],
            ("publish", "aliro/front_door/status", "offline", 1, True),
        )
        self.assertEqual(client.calls[-2:], [("loop_stop",), ("disconnect",)])

    def test_broker_reconnect_reannounces_only_retained_state(self):
        publisher, client = self.publisher()
        publisher.start()
        initial_count = len([call for call in client.calls if call[0] == "publish"])
        client.on_disconnect(client, None, None)
        client.on_connect(client, None, None, 0)
        publications = [call for call in client.calls if call[0] == "publish"]
        self.assertEqual(len(publications), initial_count + 3)
        self.assertTrue(all(call[4] for call in publications[-3:]))

    def test_start_failure_stops_an_already_started_loop(self):
        client = FailingPublishMqttClient()
        publisher = MqttPublisher(
            MqttConfig(
                host="broker.example.invalid",
                username="agent",
                password_env="OPENALIRO_HA_MQTT_PASSWORD",
            ),
            "front_door",
            client_factory=lambda: client,
            environment={"OPENALIRO_HA_MQTT_PASSWORD": "not-a-real-secret"},
        )
        with self.assertRaisesRegex(MqttError, "connection or setup failed"):
            publisher.start()
        self.assertEqual(client.calls[-2:], [("loop_stop",), ("disconnect",)])

    def test_start_reports_a_broker_rejection_after_connack(self):
        client = RejectedMqttClient()
        publisher = MqttPublisher(
            MqttConfig(
                host="broker.example.invalid",
                username="agent",
                password_env="OPENALIRO_HA_MQTT_PASSWORD",
            ),
            "front_door",
            client_factory=lambda: client,
            environment={"OPENALIRO_HA_MQTT_PASSWORD": "not-a-real-secret"},
        )
        with self.assertRaisesRegex(MqttError, "broker rejected"):
            publisher.start()
        self.assertEqual(client.calls[-2:], [("loop_stop",), ("disconnect",)])


if __name__ == "__main__":
    unittest.main(verbosity=1)
