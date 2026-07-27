#!/usr/bin/env python3
"""HA=1-only tests for the async, fake-I/O serial console session."""

import asyncio
import os
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha import (  # noqa: E402
    AccessEvent,
    CompatibilityRangeReading,
    DistanceReading,
    SerialSession,
    SessionState,
)


class FakeSerial:
    def __init__(self, responses):
        self._responses = responses
        self._lines = asyncio.Queue()
        self.closed = False
        self.writes = []

    async def readline(self):
        item = await self._lines.get()
        if isinstance(item, Exception):
            raise item
        return item

    async def write(self, data):
        self.writes.append(data)
        for line in self._responses.get(data.decode("ascii").strip(), ()):
            self._lines.put_nowait(line.encode("utf-8"))

    def close(self):
        self.closed = True

    def disconnect(self):
        self._lines.put_nowait(b"")


async def make_factory(serials):
    return serials.pop(0)


def response_map(*, stream=True):
    return {
        "aliro version": ("commit <redacted>\n",),
        "aliro status": ("chip 0xDECA0302 DW3110\n",),
        "aliro frames on": (
            "per-block distance stream \u25cf on\n"
            if stream
            else "per-block distance stream \u25cb off\n",
        ),
    }


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class SerialSessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_streaming_probe_uses_acknowledgement_not_idle_range_output(self):
        serial = FakeSerial(response_map())
        session = SerialSession(lambda: make_factory([serial]))

        self.assertEqual(await session.start(), SessionState.READY_STREAMING)
        self.assertEqual(
            serial.writes,
            [b"aliro version\n", b"aliro status\n", b"aliro frames on\n"],
        )
        await session.close()
        self.assertTrue(serial.closed)

    async def test_unsolicited_access_and_distance_are_preserved_during_commands(self):
        responses = response_map()
        responses["aliro range"] = (
            "ACCESS GRANTED for credential <redacted>\n",
            "rng  blk=7   d=123mm  tof=26\n",
            "distance 12 cm\n",
            "peer 0x<redacted>\n",
            "nlos no\n",
            "block 7\n",
            "age 20 ms\n",
            "trusted yes \u25cf\n",
        )
        serial = FakeSerial(responses)
        session = SerialSession(lambda: make_factory([serial]))
        await session.start()

        self.assertEqual(
            await session.poll_compatibility_range(),
            CompatibilityRangeReading(7, 120, 20, trusted=True, nlos=False),
        )
        self.assertEqual(session.observations.get_nowait(), AccessEvent("granted"))
        self.assertEqual(session.observations.get_nowait(), DistanceReading(7, 123, 26))
        await session.close()

    async def test_no_valid_compatibility_range_is_safe_and_terminal(self):
        responses = response_map(stream=False)
        responses["aliro range"] = ("no valid range since boot\n",)
        serial = FakeSerial(responses)
        session = SerialSession(lambda: make_factory([serial]))
        self.assertEqual(await session.start(), SessionState.READY_COMPATIBILITY)
        self.assertIsNone(await session.poll_compatibility_range())
        await session.close()

    async def test_disconnect_transitions_without_retaining_console_text(self):
        serial = FakeSerial(response_map())
        session = SerialSession(lambda: make_factory([serial]))
        await session.start()
        serial.disconnect()
        for _ in range(20):
            if session.state is SessionState.DISCONNECTED:
                break
            await asyncio.sleep(0)
        self.assertEqual(session.state, SessionState.DISCONNECTED)
        await session.close()

    async def test_maintain_reopens_after_disconnect(self):
        first = FakeSerial(response_map())
        second = FakeSerial(response_map())
        serials = [first, second]
        session = SerialSession(lambda: make_factory(serials), command_timeout=1)
        stop = asyncio.Event()
        task = asyncio.create_task(session.maintain(stop, retry_delay=0.001))
        for _ in range(100):
            if session.state is SessionState.READY_STREAMING:
                break
            await asyncio.sleep(0.001)
        self.assertEqual(session.state, SessionState.READY_STREAMING)
        first.disconnect()
        for _ in range(100):
            if len(second.writes) == 3 and session.state is SessionState.READY_STREAMING:
                break
            await asyncio.sleep(0.001)
        self.assertFalse(task.done())
        self.assertEqual(session.state, SessionState.READY_STREAMING)
        stop.set()
        await task
        self.assertEqual(second.writes[:3], [b"aliro version\n", b"aliro status\n", b"aliro frames on\n"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
