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

### `session_for_device(device: DeviceConfig) -> SerialSession`
`integration/homeassistant/src/openaliro_ha/agent.py:41`

Build a reconnecting serial session that resolves ``auto`` on every open.

**called by** `probe_device`

### `doctor(config: AgentConfig, *, publisher_factory: PublisherFactory=_publisher, session_factory: Callable[[DeviceConfig], SerialSession]=session_for_device) -> tuple[DoctorDeviceResult, ...]`
`integration/homeassistant/src/openaliro_ha/agent.py:51`

Validate serial protocol and broker connectivity without publishing events.

**calls** `AgentError`, `DoctorDeviceResult`

### `probe_device(device: DeviceConfig) -> SessionState`
`integration/homeassistant/src/openaliro_ha/agent.py:86`

Probe one selected port before configuration persists any settings.

**calls** `AgentError`, `session_for_device`

### `class _DistanceThrottle`
`integration/homeassistant/src/openaliro_ha/agent.py:100`

Rate-limit distance publishes without hiding real movement.

Ranging emits one reading per block, roughly every 192 ms, which is far more
than Home Assistant needs. Publish at most once per interval, but let a large
change through immediately so an approach or retreat is never delayed.

**called by** `run_device`

#### `_DistanceThrottle.allows(self, distance_mm: int) -> bool`
`integration/homeassistant/src/openaliro_ha/agent.py:121`

Report whether this reading should be published, recording it if so.

**called by** `run_device`

### `run_device(config: AgentConfig, device: DeviceConfig, stop_event: asyncio.Event, *, publisher_factory: PublisherFactory=_publisher, session_factory: Callable[[DeviceConfig], SerialSession]=session_for_device) -> None`
`integration/homeassistant/src/openaliro_ha/agent.py:137`

Publish one device's approved observations until a caller stops the agent.

**called by** `run`  ·  **calls** `AgentError`, `_DistanceThrottle`, `_DistanceThrottle.allows`

### `run(config: AgentConfig, stop_event: Optional[asyncio.Event]=None) -> None`
`integration/homeassistant/src/openaliro_ha/agent.py:185`

Run every configured lock concurrently until the supplied event is set.

**calls** `run_device`

<details><summary>Undocumented (3)</summary>

- `_publisher`
- `connection_factory`
- `_DistanceThrottle.__init__`

</details>
