"""pyserial adapter and privacy-safe serial-port identity helpers."""

from __future__ import annotations

import asyncio
import hashlib
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import Any, Optional


class SerialTransportError(RuntimeError):
    """A serial transport error that never includes a device path or USB serial."""


@dataclass(frozen=True)
class SerialPort:
    """One discoverable serial port with a non-reversible stable identity."""

    device: str
    identity: Optional[str]
    vid: Optional[int]
    pid: Optional[int]
    interface: Optional[str]
    product: Optional[str]


def _import_serial() -> Any:
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as error:
        raise SerialTransportError("serial support requires the packaged pyserial dependency") from error
    return serial, list_ports


def serial_identity(
    *,
    vid: Optional[int],
    pid: Optional[int],
    serial_number: Optional[str],
    interface: Optional[str],
) -> Optional[str]:
    """Hash USB identity components without retaining the USB serial number."""

    if vid is None or pid is None or not serial_number:
        return None
    material = "\x1f".join(
        (f"{vid:04x}", f"{pid:04x}", serial_number, interface or "")
    ).encode("utf-8")
    return hashlib.sha256(material).hexdigest()[:24]


def discover_serial_ports(
    ports: Optional[Iterable[Any]] = None,
) -> tuple[SerialPort, ...]:
    """List ports without exposing their paths or USB serials in diagnostics."""

    if ports is None:
        _, list_ports = _import_serial()
        ports = list_ports.comports()
    discovered = []
    for port in ports:
        vid = getattr(port, "vid", None)
        pid = getattr(port, "pid", None)
        serial_number = getattr(port, "serial_number", None)
        interface = getattr(port, "interface", None)
        discovered.append(
            SerialPort(
                device=getattr(port, "device"),
                identity=serial_identity(
                    vid=vid,
                    pid=pid,
                    serial_number=serial_number,
                    interface=interface,
                ),
                vid=vid,
                pid=pid,
                interface=interface,
                product=getattr(port, "product", None),
            )
        )
    return tuple(discovered)


def resolve_serial_port(
    serial_port: str,
    serial_identity_value: Optional[str],
    *,
    ports: Optional[Iterable[SerialPort]] = None,
) -> str:
    """Resolve an explicit path or one unambiguous privacy-safe USB identity."""

    if serial_port != "auto":
        return serial_port
    if not serial_identity_value:
        raise SerialTransportError("automatic serial selection requires a recorded USB identity")
    candidates = ports if ports is not None else discover_serial_ports()
    matching = [port.device for port in candidates if port.identity == serial_identity_value]
    if len(matching) != 1:
        raise SerialTransportError("automatic serial selection did not find one matching interface")
    return matching[0]


class PySerialConnection:
    """Async wrapper around one opened pyserial connection."""

    def __init__(self, connection: Any) -> None:
        self._connection = connection
        self._closed = False

    async def readline(self) -> bytes:
        """Wait for a line without blocking the caller's asyncio event loop."""

        while not self._closed:
            try:
                data = await asyncio.to_thread(self._connection.readline)
            except Exception as error:
                raise OSError("serial read failed") from error
            if data:
                return data
        return b""

    async def write(self, data: bytes) -> None:
        """Write a bounded command without blocking the asyncio event loop."""

        if self._closed:
            raise OSError("serial connection is closed")
        try:
            await asyncio.to_thread(self._connection.write, data)
        except Exception as error:
            raise OSError("serial write failed") from error

    def close(self) -> None:
        """Release the serial port and unblock any pending read."""

        if self._closed:
            return
        self._closed = True
        try:
            self._connection.close()
        except Exception as error:
            raise SerialTransportError("serial connection could not be closed") from error


async def open_serial_connection(
    serial_port: str,
    baud: int,
    *,
    serial_factory: Optional[Callable[..., Any]] = None,
) -> PySerialConnection:
    """Open one serial port with finite read and write timeouts."""

    if baud <= 0:
        raise ValueError("baud must be positive")
    if serial_factory is None:
        serial, _ = _import_serial()
        serial_factory = serial.Serial
    try:
        connection = await asyncio.to_thread(
            serial_factory,
            serial_port,
            baudrate=baud,
            timeout=0.2,
            write_timeout=3,
        )
    except Exception as error:
        raise SerialTransportError("serial port could not be opened") from error
    return PySerialConnection(connection)
