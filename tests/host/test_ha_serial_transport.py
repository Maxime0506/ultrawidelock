#!/usr/bin/env python3
"""HA=1-only tests for real-transport boundaries without a physical port."""

import asyncio
import errno
import os
import sys
import unittest
from types import SimpleNamespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.serial_transport import (  # noqa: E402
    SerialTransportError,
    discover_serial_ports,
    open_serial_connection,
    resolve_serial_port,
    serial_identity,
)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class SerialTransportTests(unittest.IsolatedAsyncioTestCase):
    def test_identity_is_stable_and_does_not_reveal_usb_serial(self):
        identity = serial_identity(
            vid=0x1366,
            pid=0x1051,
            serial_number="private-usb-serial",
            interface="VCOM1",
        )
        self.assertEqual(identity, serial_identity(
            vid=0x1366,
            pid=0x1051,
            serial_number="private-usb-serial",
            interface="VCOM1",
        ))
        self.assertNotIn("private-usb-serial", identity)

    def test_auto_resolution_selects_one_hashed_identity(self):
        identity = serial_identity(vid=1, pid=2, serial_number="board", interface="VCOM1")
        ports = discover_serial_ports(
            [
                SimpleNamespace(
                    device="private-path-a",
                    vid=1,
                    pid=2,
                    serial_number="board",
                    interface="VCOM0",
                    product="J-Link",
                ),
                SimpleNamespace(
                    device="private-path-b",
                    vid=1,
                    pid=2,
                    serial_number="board",
                    interface="VCOM1",
                    product="J-Link",
                ),
            ]
        )
        self.assertEqual(resolve_serial_port("auto", identity, ports=ports), "private-path-b")
        with self.assertRaises(SerialTransportError):
            resolve_serial_port("auto", None, ports=ports)

    def test_unknown_interface_uses_endpoint_fingerprint_without_collapsing_ports(self):
        ports = discover_serial_ports(
            [
                SimpleNamespace(
                    device="private-path-a",
                    vid=1,
                    pid=2,
                    serial_number="board",
                    interface=None,
                    product="J-Link",
                ),
                SimpleNamespace(
                    device="private-path-b",
                    vid=1,
                    pid=2,
                    serial_number="board",
                    interface=None,
                    product="J-Link",
                ),
            ]
        )
        self.assertNotEqual(ports[0].identity, ports[1].identity)
        self.assertEqual(resolve_serial_port("auto", ports[1].identity, ports=ports), "private-path-b")

    async def test_open_read_write_and_close_stay_off_the_event_loop(self):
        class FakeSerial:
            def __init__(self, port, **kwargs):
                self.port = port
                self.kwargs = kwargs
                self.writes = []
                self.closed = False
                self.lines = [b"console line\\n"]

            def readline(self):
                return self.lines.pop(0) if self.lines else b""

            def write(self, data):
                self.writes.append(data)

            def close(self):
                self.closed = True

        created = []

        def factory(*args, **kwargs):
            connection = FakeSerial(*args, **kwargs)
            created.append(connection)
            return connection

        connection = await open_serial_connection("private-path", 115200, serial_factory=factory)
        self.assertEqual(await connection.readline(), b"console line\\n")
        await connection.write(b"aliro version\\n")
        connection.close()
        self.assertTrue(created[0].closed)
        self.assertTrue(created[0].kwargs["exclusive"])

    async def test_busy_port_has_a_specific_redacted_error(self):
        def factory(*_args, **_kwargs):
            raise OSError(errno.EBUSY, "private-path")

        with self.assertRaisesRegex(SerialTransportError, "^serial port is busy$"):
            await open_serial_connection("private-path", 115200, serial_factory=factory)

    async def test_open_failure_is_redacted(self):
        def factory(*_args, **_kwargs):
            raise RuntimeError("private-path")

        with self.assertRaisesRegex(SerialTransportError, "could not be opened") as raised:
            await open_serial_connection("private-path", 115200, serial_factory=factory)
        self.assertNotIn("private-path", str(raised.exception))


if __name__ == "__main__":
    unittest.main(verbosity=1)
