"""Standalone MQTT adapter for the HA=1 staging agent.

The parser and serial session do not import this module. MQTT remains an agent
transport, with the current discovery and state topics kept stable.
"""

import json
import os
import threading
from pathlib import Path
from typing import Callable, Optional, Protocol

from .config import MqttConfig
from .models import AccessEvent, DistanceReading


DISCOVERY_QOS = 1
AVAILABILITY_QOS = 1
OBSERVATION_QOS = 0
MAX_PASSWORD_FILE_BYTES = 4096
CONNECTION_TIMEOUT_SECONDS = 10


class MqttError(RuntimeError):
    """An MQTT error that intentionally omits broker and secret values."""


class MqttClient(Protocol):
    """The small paho client surface needed by the standalone adapter."""

    def username_pw_set(self, username: str, password: str) -> None: ...

    def tls_set(self, **kwargs: str) -> None: ...

    def tls_insecure_set(self, value: bool) -> None: ...

    def will_set(self, topic: str, payload: str, qos: int, retain: bool) -> None: ...

    def connect(self, host: str, port: int, keepalive: int) -> None: ...

    def loop_start(self) -> None: ...

    def reconnect_delay_set(self, min_delay: int, max_delay: int) -> None: ...

    def loop_stop(self) -> None: ...

    def disconnect(self) -> None: ...

    def publish(self, topic: str, payload: str, qos: int, retain: bool) -> object: ...


def discovery_payloads(node: str) -> list[tuple[str, dict[str, object]]]:
    """Return the legacy MQTT Discovery contract for one configured device."""

    device = {
        "identifiers": [node],
        "name": node,
        "manufacturer": "openaliro",
        "model": "nRF5340 Aliro lock",
    }
    base = f"aliro/{node}"
    return [
        (
            f"homeassistant/sensor/{node}/distance/config",
            {
                "name": "Distance",
                "unique_id": f"{node}_distance",
                "state_topic": f"{base}/distance",
                "availability_topic": f"{base}/status",
                "unit_of_measurement": "mm",
                "device_class": "distance",
                "state_class": "measurement",
                "device": device,
            },
        ),
        (
            f"homeassistant/event/{node}/access/config",
            {
                "name": "Access",
                "unique_id": f"{node}_access",
                "state_topic": f"{base}/access",
                "availability_topic": f"{base}/status",
                "event_types": ["granted", "denied"],
                "device": device,
            },
        ),
    ]


def resolve_password(config: MqttConfig, environment: Optional[dict[str, str]] = None) -> str:
    """Resolve exactly one configured password reference without logging its value."""

    if config.password_env:
        value = (environment if environment is not None else os.environ).get(config.password_env)
        if not value:
            raise MqttError("configured MQTT password environment variable is unavailable")
        return value
    if config.password_file:
        try:
            value = Path(config.password_file).expanduser().read_bytes()
        except OSError as error:
            raise MqttError("configured MQTT password file is unavailable") from error
        if len(value) > MAX_PASSWORD_FILE_BYTES:
            raise MqttError("configured MQTT password file exceeds the size limit")
        try:
            decoded = value.decode("utf-8").rstrip("\r\n")
        except UnicodeDecodeError as error:
            raise MqttError("configured MQTT password file is not valid UTF-8") from error
        if not decoded:
            raise MqttError("configured MQTT password file is empty")
        return decoded
    raise MqttError("configured MQTT password source is unavailable")


def _expanded_path(value: str) -> str:
    """Return a user-facing configured path without requiring an absolute home path."""

    return str(Path(value).expanduser())


def _default_client_factory() -> MqttClient:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as error:
        raise MqttError("MQTT support requires the packaged paho-mqtt dependency") from error
    return mqtt.Client()


class MqttPublisher:
    """Publish discovery, availability, distance, and access observations."""

    def __init__(
        self,
        config: MqttConfig,
        node: str,
        *,
        client_factory: Callable[[], MqttClient] = _default_client_factory,
        environment: Optional[dict[str, str]] = None,
    ) -> None:
        if not config.tls and not config.allow_insecure:
            raise MqttError("plaintext MQTT requires explicit insecure opt-in")
        if bool(config.client_cert) != bool(config.client_key):
            raise MqttError("MQTT client certificate and key must be configured together")
        if not (config.username or config.client_cert or config.allow_anonymous):
            raise MqttError("MQTT requires authentication or explicit anonymous opt-in")
        self._config = config
        self._node = node
        self._client_factory = client_factory
        self._environment = environment
        self._client: Optional[MqttClient] = None
        self._announced = False
        self._connection_event = threading.Event()
        self._connection_error: Optional[str] = None

    @property
    def status_topic(self) -> str:
        return f"aliro/{self._node}/status"

    def start(self) -> None:
        """Connect securely, then announce retained discovery and availability."""

        client = self._client_factory()
        connected = False
        loop_started = False
        try:
            self._connection_event.clear()
            self._connection_error = None
            if self._config.username:
                client.username_pw_set(
                    self._config.username,
                    resolve_password(self._config, self._environment),
                )
            if self._config.tls:
                tls_options = {
                    key: value
                    for key, value in {
                        "ca_certs": _expanded_path(self._config.ca_path)
                        if self._config.ca_path
                        else None,
                        "certfile": _expanded_path(self._config.client_cert)
                        if self._config.client_cert
                        else None,
                        "keyfile": _expanded_path(self._config.client_key)
                        if self._config.client_key
                        else None,
                    }.items()
                    if value is not None
                }
                client.tls_set(**tls_options)
                client.tls_insecure_set(False)
            client.reconnect_delay_set(min_delay=1, max_delay=60)
            client.on_connect = self._on_connect
            client.on_disconnect = self._on_disconnect
            client.will_set(self.status_topic, "offline", qos=AVAILABILITY_QOS, retain=True)
            client.connect(self._config.host, self._config.port, keepalive=60)
            connected = True
            self._client = client
            loop_started = True
            client.loop_start()
            if not self._connection_event.wait(CONNECTION_TIMEOUT_SECONDS):
                raise MqttError("MQTT broker did not acknowledge the connection")
            if self._connection_error:
                raise MqttError(self._connection_error)
        except Exception as error:
            if self._client is client:
                self._client = None
                self._announced = False
            if loop_started:
                try:
                    client.loop_stop()
                except Exception:
                    pass
            if connected:
                try:
                    client.disconnect()
                except Exception:
                    pass
            if isinstance(error, MqttError):
                raise
            raise MqttError("MQTT connection or setup failed") from error
        self._client = client

    def publish_distance(self, reading: DistanceReading) -> None:
        """Publish a fresh distance sample without retaining it."""

        self._publish(f"aliro/{self._node}/distance", str(reading.distance_mm))

    def publish_access(self, event: AccessEvent) -> None:
        """Publish a credential-independent access event without retaining it."""

        self._publish(
            f"aliro/{self._node}/access",
            json.dumps({"event_type": event.verdict}),
        )

    def _publish(self, topic: str, payload: str) -> None:
        if self._client is None:
            raise MqttError("MQTT publisher is not connected")
        try:
            self._client.publish(topic, payload, qos=OBSERVATION_QOS, retain=False)
        except Exception as error:
            raise MqttError("MQTT publish failed") from error

    def _announce(self) -> None:
        """Publish retained discovery and online availability once per connection."""

        if self._client is None or self._announced:
            return
        for topic, payload in discovery_payloads(self._node):
            self._client.publish(topic, json.dumps(payload), qos=DISCOVERY_QOS, retain=True)
        self._client.publish(
            self.status_topic,
            "online",
            qos=AVAILABILITY_QOS,
            retain=True,
        )
        self._announced = True

    def _on_connect(
        self,
        client: MqttClient,
        _userdata: object,
        _flags: object,
        reason_code: object,
        *_: object,
    ) -> None:
        """Re-announce retained discovery after paho reconnects in its loop thread."""

        failed = getattr(reason_code, "is_failure", None)
        accepted = not failed if isinstance(failed, bool) else reason_code == 0
        if not accepted:
            self._connection_error = "MQTT broker rejected the connection"
            self._connection_event.set()
            return
        self._connection_event.set()
        if client is self._client:
            self._announce()

    def _on_disconnect(self, client: MqttClient, *_: object) -> None:
        """Allow the next successful connection to refresh retained state."""

        if client is self._client:
            self._announced = False

    def close(self) -> None:
        """Publish offline availability before stopping the broker loop."""

        if self._client is None:
            return
        client, self._client = self._client, None
        self._announced = False
        try:
            client.publish(
                self.status_topic,
                "offline",
                qos=AVAILABILITY_QOS,
                retain=True,
            )
            client.loop_stop()
            client.disconnect()
        except Exception as error:
            raise MqttError("MQTT shutdown failed") from error
