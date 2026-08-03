<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/serial_session.py`

Async, transport-neutral ownership of one OpenAliro serial console.

The session is deliberately independent of pyserial and Home Assistant. A
runtime adapter provides an opened byte-stream; this module serializes shell
commands, parses only the approved observations, and never retains raw console
lines. The device can be idle after ``aliro frames on``: a stream acknowledgement
is therefore the capability probe, not the first range reading.

**depends on** [`integration/homeassistant/src/openaliro_ha/compatibility.py`](compatibility.md), [`integration/homeassistant/src/openaliro_ha/models.py`](models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

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

### `class _ResponseHandler(Protocol)`
`integration/homeassistant/src/openaliro_ha/serial_session.py:55`

Protocol for command response handlers: feed one line of console output and return a tuple of (is_complete, result), where is_complete indicates the full response has arrived.

#### `_ResponseHandler.feed(self, line: str) -> tuple[bool, object]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:57`

Return whether a command response is complete and its safe result.

**called by** `SerialSession._feed_response`

### `class _ContainsResponse`
`integration/homeassistant/src/openaliro_ha/serial_session.py:64`

Handler that returns true and a provided result as soon as the expected substring appears anywhere in a line, ignoring ANSI escape codes.

**called by** `SerialSession.start`

#### `_ContainsResponse.__init__(self, expected: str, result: Result) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:66`

Record the substring to match and the result to return when the match is found.

#### `_ContainsResponse.feed(self, line: str) -> tuple[bool, object]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:71`

Return whether the expected substring is present in the line after stripping ANSI codes, and the pre-set result.

### `class _StreamResponse`
`integration/homeassistant/src/openaliro_ha/serial_session.py:76`

Accept the firmware's frames acknowledgement and retain its actual mode.

**called by** `SerialSession.start`

#### `_StreamResponse.feed(self, line: str) -> tuple[bool, object]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:81`

Return whether the ACKNOWLEDGEMENT text is present in the line, and if so whether the response indicates the stream is on or off.

### `class _RangeResponse`
`integration/homeassistant/src/openaliro_ha/serial_session.py:89`

Handler that parses the multiline output of the aliro range command and returns true with the parsed RangeReading when available, or true with None when the parser confirms the response is finished.

**called by** `SerialSession.poll_compatibility_range`

#### `_RangeResponse.__init__(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:91`

Initialize the handler and prime the internal range response parser.

#### `_RangeResponse.feed(self, line: str) -> tuple[bool, object]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:96`

Feed a line to the internal parser; return true with a RangeReading if a complete range measurement is available, true with None if the parser confirms the response finished, or false if more input is needed.

### `class SerialSession`
`integration/homeassistant/src/openaliro_ha/serial_session.py:106`

Read console observations while issuing one shell command at a time.

#### `SerialSession.__init__(self, connection_factory: ConnectionFactory, *, command_timeout: float=3.0, observation_queue_size: int=256) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:109`

Initialize a serial session with the given connection factory and optional command and queue limits. Raises ValueError if command_timeout or observation_queue_size is not positive.

#### `SerialSession.state(self) -> SessionState`
`integration/homeassistant/src/openaliro_ha/serial_session.py:133`

Return the current lifecycle state.

#### `SerialSession.observations(self) -> asyncio.Queue[Observation]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:139`

Expose parsed observations without exposing the raw console.

#### `SerialSession.start(self) -> SessionState`
`integration/homeassistant/src/openaliro_ha/serial_session.py:144`

Open, probe, and prepare the console without changing lock state.

**called by** `SerialSession.maintain`  ·  **calls** `SerialConnection.close`, `SerialSession._command`, `SerialSession._read_loop`, `SerialSessionError`, `_ContainsResponse`, `_StreamResponse`

#### `SerialSession.poll_compatibility_range(self) -> Optional[CompatibilityRangeReading]`
`integration/homeassistant/src/openaliro_ha/serial_session.py:174`

Read one ``aliro range`` response while preserving unsolicited events.

**calls** `SerialSession._command`, `SerialSessionError`, `_RangeResponse`

#### `SerialSession.maintain(self, stop_event: asyncio.Event, *, retry_delay: float=1.0) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:186`

Reconnect until stopped, with a bounded caller-selected delay.

**calls** `SerialConnection.close`, `SerialSession._wait_for_disconnect_or_stop`, `SerialSession.start`

#### `SerialSession._wait_for_disconnect_or_stop(self, stop_event: asyncio.Event) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:207`

Wake promptly for either a transport loss or a caller-requested stop.

**called by** `SerialSession.maintain`

#### `SerialSession.close(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:222`

Stop I/O, fail any pending command, and close the owned transport.

**calls** `SerialConnection.close`, `SerialSession._fail_pending`, `SerialSessionError`

#### `SerialSession._command(self, command: str, handler: _ResponseHandler) -> object`
`integration/homeassistant/src/openaliro_ha/serial_session.py:239`

Send a command string to the serial console, invoke the given response handler on each line of reply, and return the handler's result when the response is complete. Raises SerialSessionError if the connection is closed or the command times out.

**called by** `SerialSession.poll_compatibility_range`, `SerialSession.start`  ·  **calls** `SerialConnection.write`, `SerialSessionError`

#### `SerialSession._read_loop(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:259`

Coroutine: read lines from the serial console indefinitely until disconnected. Parse each line for observations (distance/access events) and enqueue them; feed unparsed lines to the current command's response handler. On disconnect or queue overflow, fail all pending responses and mark the session disconnected.

**called by** `SerialSession.start`  ·  **calls** `SerialConnection.readline`, `SerialSession._fail_pending`, `SerialSession._feed_response`, `SerialSessionError`

#### `SerialSession._feed_response(self, line: str) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:281`

Feed a line of console output to the current command's response handler, and set the result on the pending future if the handler reports the response is complete.

**called by** `SerialSession._read_loop`  ·  **calls** `_ResponseHandler.feed`

#### `SerialSession._fail_pending(self, error: SerialSessionError) -> None`
`integration/homeassistant/src/openaliro_ha/serial_session.py:290`

Set an exception on any pending command response future to signal that the response will not arrive, used when the connection fails or is closed.

**called by** `SerialSession._read_loop`, `SerialSession.close`
