<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/agent.py`

Runnable standalone-agent orchestration over the shared serial library.

**depends on** [`integration/homeassistant/src/openaliro_ha/config.py`](config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](models.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](mqtt.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](serial_session.md), [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](serial_transport.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class AgentError(RuntimeError)`
`integration/homeassistant/src/openaliro_ha/agent.py:22`

A safe agent failure with no raw serial, broker, or console details.

**called by** `doctor`, `probe_device`, `run_device`

### `class DoctorDeviceResult`
`integration/homeassistant/src/openaliro_ha/agent.py:27`

One diagnostics-safe device result.

**called by** `doctor`

### `_publisher(config: AgentConfig, device: DeviceConfig) -> MqttPublisher`
`integration/homeassistant/src/openaliro_ha/agent.py:37`

Create an MQTT publisher from the agent and device configuration.

### `session_for_device(device: DeviceConfig) -> SerialSession`
`integration/homeassistant/src/openaliro_ha/agent.py:42`

Build a reconnecting serial session that resolves ``auto`` on every open.

**called by** `probe_device`

### `connection_factory()`
`integration/homeassistant/src/openaliro_ha/agent.py:45`

Factory coroutine to open a serial connection to the device: resolve the serial port from device.serial_port and device.serial_identity, then open the connection at device.baud.

### `doctor(config: AgentConfig, *, publisher_factory: PublisherFactory=_publisher, session_factory: Callable[[DeviceConfig], SerialSession]=session_for_device) -> tuple[DoctorDeviceResult, ...]`
`integration/homeassistant/src/openaliro_ha/agent.py:53`

Validate serial protocol and broker connectivity without publishing events.

**calls** `AgentError`, `DoctorDeviceResult`

### `probe_device(device: DeviceConfig) -> SessionState`
`integration/homeassistant/src/openaliro_ha/agent.py:88`

Probe one selected port before configuration persists any settings.

**calls** `AgentError`, `session_for_device`

### `class _DistanceThrottle`
`integration/homeassistant/src/openaliro_ha/agent.py:102`

Rate-limit distance publishes without hiding real movement.

Ranging emits one reading per block, roughly every 192 ms, which is far more
than Home Assistant needs. Publish at most once per interval, but let a large
change through immediately so an approach or retreat is never delayed.

**called by** `run_device`

#### `_DistanceThrottle.__init__(self, *, min_interval: float=DISTANCE_MIN_INTERVAL_S, significant_change_mm: int=DISTANCE_SIGNIFICANT_CHANGE_MM, clock: Callable[[], float]=time.monotonic) -> None`
`integration/homeassistant/src/openaliro_ha/agent.py:110`

Initialize the distance throttle with optional minimum publish interval in seconds, minimum significant change in millimeters, and a clock function. Default min_interval is DISTANCE_MIN_INTERVAL_S, default significant_change_mm is DISTANCE_SIGNIFICANT_CHANGE_MM, and default clock is time.monotonic.

#### `_DistanceThrottle.allows(self, distance_mm: int) -> bool`
`integration/homeassistant/src/openaliro_ha/agent.py:124`

Report whether this reading should be published, recording it if so.

**called by** `run_device`

### `run_device(config: AgentConfig, device: DeviceConfig, stop_event: asyncio.Event, *, publisher_factory: PublisherFactory=_publisher, session_factory: Callable[[DeviceConfig], SerialSession]=session_for_device) -> None`
`integration/homeassistant/src/openaliro_ha/agent.py:140`

Publish one device's approved observations until a caller stops the agent.

**called by** `run`  ·  **calls** `AgentError`, `_DistanceThrottle`, `_DistanceThrottle.allows`

### `run(config: AgentConfig, stop_event: Optional[asyncio.Event]=None) -> None`
`integration/homeassistant/src/openaliro_ha/agent.py:188`

Run every configured lock concurrently until the supplied event is set.

**calls** `run_device`
