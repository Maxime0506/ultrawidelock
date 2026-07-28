<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/mqtt.py`

Standalone MQTT adapter for the HA=1 staging agent.

The parser and serial session do not import this module. MQTT remains an agent
transport, with the current discovery and state topics kept stable.

**depends on** [`integration/homeassistant/src/openaliro_ha/config.py`](config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](models.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class MqttError(RuntimeError)`
`integration/homeassistant/src/openaliro_ha/mqtt.py:24`

An MQTT error that intentionally omits broker and secret values.

**called by** `MqttPublisher.__init__`, `MqttPublisher._publish`, `MqttPublisher.close`, `MqttPublisher.start`, `_default_client_factory`, `resolve_password`

### `class MqttClient(Protocol)`
`integration/homeassistant/src/openaliro_ha/mqtt.py:28`

The small paho client surface needed by the standalone adapter.

### `discovery_payloads(node: str) -> list[tuple[str, dict[str, object]]]`
`integration/homeassistant/src/openaliro_ha/mqtt.py:52`

Return the legacy MQTT Discovery contract for one configured device.

**called by** `MqttPublisher._announce`

### `resolve_password(config: MqttConfig, environment: Optional[dict[str, str]]=None) -> str`
`integration/homeassistant/src/openaliro_ha/mqtt.py:90`

Resolve exactly one configured password reference without logging its value.

**called by** `MqttPublisher.start`  ·  **calls** `MqttError`

### `_expanded_path(value: str) -> str`
`integration/homeassistant/src/openaliro_ha/mqtt.py:115`

Return a user-facing configured path without requiring an absolute home path.

**called by** `MqttPublisher.start`

### `class MqttPublisher`
`integration/homeassistant/src/openaliro_ha/mqtt.py:129`

Publish discovery, availability, distance, and access observations.

#### `MqttPublisher.start(self) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:159`

Connect securely, then announce retained discovery and availability.

**calls** `MqttClient.connect`, `MqttClient.disconnect`, `MqttClient.loop_start`, `MqttClient.loop_stop`, `MqttClient.reconnect_delay_set`, `MqttClient.tls_insecure_set`, `MqttClient.tls_set`, `MqttClient.username_pw_set`

#### `MqttPublisher.publish_distance(self, reading: DistanceReading) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:223`

Publish a fresh distance sample without retaining it.

**calls** `MqttPublisher._publish`

#### `MqttPublisher.publish_access(self, event: AccessEvent) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:228`

Publish a credential-independent access event without retaining it.

**calls** `MqttPublisher._publish`

#### `MqttPublisher._announce(self) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:244`

Publish retained discovery and online availability once per connection.

**called by** `MqttPublisher._on_connect`  ·  **calls** `MqttClient.publish`, `discovery_payloads`

#### `MqttPublisher._on_connect(self, client: MqttClient, _userdata: object, _flags: object, reason_code: object, *_: object) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:259`

Re-announce retained discovery after paho reconnects in its loop thread.

**calls** `MqttPublisher._announce`

#### `MqttPublisher._on_disconnect(self, client: MqttClient, *_: object) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:279`

Allow the next successful connection to refresh retained state.

#### `MqttPublisher.close(self) -> None`
`integration/homeassistant/src/openaliro_ha/mqtt.py:285`

Publish offline availability before stopping the broker loop.

**calls** `MqttClient.disconnect`, `MqttClient.loop_stop`, `MqttClient.publish`, `MqttError`

<details><summary>Undocumented (14)</summary>

- `MqttClient.username_pw_set`
- `MqttClient.tls_set`
- `MqttClient.tls_insecure_set`
- `MqttClient.will_set`
- `MqttClient.connect`
- `MqttClient.loop_start`
- `MqttClient.reconnect_delay_set`
- `MqttClient.loop_stop`
- `MqttClient.disconnect`
- `MqttClient.publish`
- `_default_client_factory`
- `MqttPublisher.__init__`
- `MqttPublisher.status_topic`
- `MqttPublisher._publish`

</details>
