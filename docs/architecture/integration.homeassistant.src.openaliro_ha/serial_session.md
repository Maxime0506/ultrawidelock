<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/serial_session.py`

Async, transport-neutral ownership of one OpenAliro serial console.

The session is deliberately independent of pyserial and Home Assistant. A
runtime adapter provides an opened byte-stream; this module serializes shell
commands, parses only the approved observations, and never retains raw console
lines. The device can be idle after ``aliro frames on``: a stream acknowledgement
is therefore the capability probe, not the first range reading.

**depends on** [`integration/homeassistant/src/openaliro_ha/compatibility.py`](compatibility.md), [`integration/homeassistant/src/openaliro_ha/models.py`](models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md)

## API

### `class SerialConnection(Protocol)`
`integration/homeassistant/src/openaliro_ha/serial_session.py:22`

The small async byte-stream contract needed by ``SerialSession``.

#### `SerialConnection.readline(self) -> bytes`
`integration/homeassistant/src/openaliro_ha/serial_session.py:25`

Return one newline-delimited serial line or ``b""`` on disconnect.

**called by** `SerialSession._read_loop`

#### `SerialConnection.write(self, data: bytes) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:28`

Queue bytes for delivery to the console.

**called by** `SerialSession._command`

#### `SerialConnection.close(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:31`

Release the serial device.

**called by** `SerialSession.close`, `SerialSession.maintain`, `SerialSession.start`

### `class SessionState(str, Enum)`
`integration/homeassistant/src/openaliro_ha/serial_session.py:38`

Externally visible lifecycle states with no raw device details.

### `class SerialSessionError(RuntimeError)`
`integration/homeassistant/src/openaliro_ha/serial_session.py:51`

A safe, user-facing serial session failure.

**called by** `SerialSession._command`, `SerialSession._read_loop`, `SerialSession.close`, `SerialSession.poll_compatibility_range`, `SerialSession.start`

#### `_ResponseHandler.feed(self, line: str) -> tuple[bool, object]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:56`

Return whether a command response is complete and its safe result.

**called by** `SerialSession._feed_response`

### `class _StreamResponse`
`integration/homeassistant/src/openaliro_ha/serial_session.py:72`

Accept the firmware's frames acknowledgement and retain its actual mode.

**called by** `SerialSession.start`

### `class SerialSession`
`integration/homeassistant/src/openaliro_ha/serial_session.py:98`

Read console observations while issuing one shell command at a time.

#### `SerialSession.state(self) -> SessionState`
`integration/homeassistant/src/openaliro_ha/serial_session.py:124`

Return the current lifecycle state.

#### `SerialSession.observations(self) -> asyncio.Queue[Observation]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:130`

Expose parsed observations without exposing the raw console.

#### `SerialSession.start(self) -> SessionState`
`integration/homeassistant/src/openaliro_ha/serial_session.py:135`

Open, probe, and prepare the console without changing lock state.

**called by** `SerialSession.maintain`  ·  **calls** `SerialConnection.close`, `SerialSession._command`, `SerialSession._read_loop`, `SerialSessionError`, `_ContainsResponse`, `_StreamResponse`

#### `SerialSession.poll_compatibility_range(self) -> Optional[CompatibilityRangeReading]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:165`

Read one ``aliro range`` response while preserving unsolicited events.

**calls** `SerialSession._command`, `SerialSessionError`, `_RangeResponse`

#### `SerialSession.maintain(self, stop_event: asyncio.Event, *, retry_delay: float=1.0) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:177`

Reconnect until stopped, with a bounded caller-selected delay.

**calls** `SerialConnection.close`, `SerialSession._wait_for_disconnect_or_stop`, `SerialSession.start`

#### `SerialSession._wait_for_disconnect_or_stop(self, stop_event: asyncio.Event) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:198`

Wake promptly for either a transport loss or a caller-requested stop.

**called by** `SerialSession.maintain`

#### `SerialSession.close(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:213`

Stop I/O, fail any pending command, and close the owned transport.

**calls** `SerialConnection.close`, `SerialSession._fail_pending`, `SerialSessionError`

<details><summary>Undocumented (13)</summary>

- `_ResponseHandler`
- `_ContainsResponse`
- `_ContainsResponse.__init__`
- `_ContainsResponse.feed`
- `_StreamResponse.feed`
- `_RangeResponse`
- `_RangeResponse.__init__`
- `_RangeResponse.feed`
- `SerialSession.__init__`
- `SerialSession._command`
- `SerialSession._read_loop`
- `SerialSession._feed_response`
- `SerialSession._fail_pending`

</details>
