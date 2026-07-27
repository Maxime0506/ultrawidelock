#!/usr/bin/env python3
"""HA=1-only tests for standalone-agent orchestration with fake transports."""

import asyncio
import os
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.agent import AgentError, doctor, run_device  # noqa: E402
from openaliro_ha.config import AgentConfig, DeviceConfig, MqttConfig  # noqa: E402
from openaliro_ha.models import AccessEvent, DistanceReading  # noqa: E402
from openaliro_ha.serial_session import SessionState  # noqa: E402


class FakeSession:
    def __init__(self, state=SessionState.READY_STREAMING):
        self.state = state
        self.observations = asyncio.Queue()
        self.closed = False

    async def start(self):
        return self.state

    async def maintain(self, stop_event):
        await stop_event.wait()

    async def close(self):
        self.closed = True


class FakePublisher:
    def __init__(self):
        self.started = False
        self.closed = False
        self.distance = []
        self.access = []

    def start(self):
        self.started = True

    def close(self):
        self.closed = True

    def publish_distance(self, reading):
        self.distance.append(reading)

    def publish_access(self, event):
        self.access.append(event)


def config():
    return AgentConfig(
        mqtt=MqttConfig(host="broker.example.invalid", allow_anonymous=True),
        devices=(DeviceConfig(device_id="front_door", serial_port="private-path"),),
    )


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class AgentTests(unittest.IsolatedAsyncioTestCase):
    async def test_doctor_checks_every_device_and_broker(self):
        session = FakeSession()
        publisher = FakePublisher()

        results = await doctor(
            config(),
            session_factory=lambda _device: session,
            publisher_factory=lambda _config, _device: publisher,
        )
        self.assertEqual(results[0].serial_state, "ready_streaming")
        self.assertTrue(session.closed)
        self.assertTrue(publisher.started)
        self.assertTrue(publisher.closed)

    async def test_doctor_redacts_transport_failures(self):
        class FailedSession(FakeSession):
            async def start(self):
                raise RuntimeError("private-path")

        with self.assertRaisesRegex(AgentError, "serial console compatibility") as raised:
            await doctor(config(), session_factory=lambda _device: FailedSession())
        self.assertNotIn("private-path", str(raised.exception))

    async def test_run_device_publishes_only_approved_observations(self):
        session = FakeSession()
        publisher = FakePublisher()
        stop = asyncio.Event()
        task = asyncio.create_task(
            run_device(
                config(),
                config().devices[0],
                stop,
                session_factory=lambda _device: session,
                publisher_factory=lambda _config, _device: publisher,
            )
        )
        await session.observations.put(DistanceReading(1, 42, 9))
        await session.observations.put(AccessEvent("granted"))
        for _ in range(20):
            if publisher.access:
                break
            await asyncio.sleep(0)
        stop.set()
        await task
        self.assertEqual(publisher.distance, [DistanceReading(1, 42, 9)])
        self.assertEqual(publisher.access, [AccessEvent("granted")])
        self.assertTrue(session.closed)
        self.assertTrue(publisher.closed)


if __name__ == "__main__":
    unittest.main(verbosity=1)
