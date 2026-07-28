<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/serial_transport.py`

pyserial adapter and privacy-safe serial-port identity helpers.

**used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class SerialTransportError(RuntimeError)`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:13`

A serial transport error that never includes a device path or USB serial.

**called by** `PySerialConnection.close`, `_import_serial`, `open_serial_connection`, `resolve_serial_port`

### `class SerialPort`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:18`

One discoverable serial port with a non-reversible stable identity.

**called by** `discover_serial_ports`

### `serial_identity(*, vid: Optional[int], pid: Optional[int], serial_number: Optional[str], interface: Optional[str], device: Optional[str]=None) -> Optional[str]`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:38`

Hash one USB console endpoint without retaining serial or path text.

Some macOS J-Link drivers omit the CDC interface name. In that case, include
the currently assigned endpoint name so two console endpoints cannot collapse
into one identity and silently select the wrong one. A later rename fails
closed and requires reconfiguration rather than guessing.

**called by** `discover_serial_ports`

### `discover_serial_ports(ports: Optional[Iterable[Any]]=None) -> tuple[SerialPort, ...]`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:63`

List ports without exposing their paths or USB serials in diagnostics.

**called by** `resolve_serial_port`  ·  **calls** `SerialPort`, `_import_serial`, `serial_identity`

### `resolve_serial_port(serial_port: str, serial_identity_value: Optional[str], *, ports: Optional[Iterable[SerialPort]]=None) -> str`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:96`

Resolve an explicit path or one unambiguous privacy-safe USB identity.

**calls** `SerialTransportError`, `discover_serial_ports`

### `class PySerialConnection`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:115`

Async wrapper around one opened pyserial connection.

**called by** `open_serial_connection`

#### `PySerialConnection.readline(self) -> bytes`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:122`

Wait for a line without blocking the caller's asyncio event loop.

#### `PySerialConnection.write(self, data: bytes) -> None`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:134`

Write a bounded command without blocking the asyncio event loop.

#### `PySerialConnection.close(self) -> None`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:144`

Release the serial port and unblock any pending read.

**calls** `SerialTransportError`

### `open_serial_connection(serial_port: str, baud: int, *, serial_factory: Optional[Callable[..., Any]]=None) -> PySerialConnection`
`integration/homeassistant/src/openaliro_ha/serial_transport.py:156`

Open one serial port with finite read and write timeouts.

**calls** `PySerialConnection`, `SerialTransportError`, `_import_serial`

<details><summary>Undocumented (2)</summary>

- `_import_serial`
- `PySerialConnection.__init__`

</details>
