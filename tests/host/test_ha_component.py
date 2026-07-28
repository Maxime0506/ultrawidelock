#!/usr/bin/env python3
"""HA=1-only tests for the direct Home Assistant custom component.

Split in two. ``runtime.py`` imports nothing from Home Assistant, so its
behaviour is tested unconditionally and is the part most worth pinning: it owns
the session, the fan-out to entities, and the shutdown path. The rest of the
component does import Home Assistant, so those cases skip unless the package is
installed and run in CI, where it is.
"""

import asyncio
import importlib.util
import os
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "integration" / "homeassistant" / "custom_components"
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))
sys.path.insert(0, str(COMPONENT))

from openaliro_ha import AccessEvent, DeviceConfig, DistanceReading  # noqa: E402
from openaliro_ha.serial_session import SessionState  # noqa: E402

HOME_ASSISTANT = importlib.util.find_spec("homeassistant") is not None


class FakeSession:
    """A session that yields queued observations and records its shutdown."""

    def __init__(self, state=SessionState.READY_STREAMING):
        self.state = state
        self.observations: asyncio.Queue = asyncio.Queue()
        self.closed = False
        self.maintained = False

    async def maintain(self, stop_event, **_kwargs):
        self.maintained = True
        await stop_event.wait()

    async def close(self):
        self.closed = True


def _load_runtime_module():
    """Import runtime.py by path.

    Importing ``openaliro.runtime`` would execute the package __init__, which
    pulls in Home Assistant. runtime.py itself depends only on the shared
    library, so loading the file directly keeps these cases runnable anywhere.
    """

    spec = importlib.util.spec_from_file_location(
        "openaliro_runtime_under_test", COMPONENT / "openaliro" / "runtime.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


OpenAliroRuntime = _load_runtime_module().OpenAliroRuntime


def runtime_for(session, **kwargs):
    return OpenAliroRuntime(
        DeviceConfig(device_id="front-door", serial_port="private-path"),
        session=session,
        **kwargs,
    )


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class RuntimeTests(unittest.IsolatedAsyncioTestCase):
    async def drain(self, runtime, session, observation):
        """Deliver one observation and wait for the consumer to apply it."""

        await session.observations.put(observation)
        for _ in range(100):
            await asyncio.sleep(0)
            if session.observations.empty():
                return
        self.fail("observation was never consumed")

    async def test_distance_reaches_entities_and_notifies_listeners(self):
        session = FakeSession()
        runtime = runtime_for(session)
        seen = []
        runtime.add_listener(lambda: seen.append(runtime.distance_mm))
        await runtime.async_start()
        try:
            await self.drain(runtime, session, DistanceReading(block=None, distance_mm=548, tof=117))
        finally:
            await runtime.async_stop()
        self.assertEqual(runtime.distance_mm, 548)
        self.assertEqual(seen, [548])

    async def test_access_event_is_recorded_and_forwarded(self):
        session = FakeSession()
        forwarded = []
        runtime = runtime_for(session, access_callback=forwarded.append)
        await runtime.async_start()
        try:
            await self.drain(runtime, session, AccessEvent(verdict="granted"))
        finally:
            await runtime.async_stop()
        self.assertEqual(runtime.last_access, AccessEvent(verdict="granted"))
        self.assertEqual(forwarded, [AccessEvent(verdict="granted")])

    async def test_a_removed_listener_stops_being_called(self):
        session = FakeSession()
        runtime = runtime_for(session)
        seen = []
        remove = runtime.add_listener(lambda: seen.append(1))
        await runtime.async_start()
        try:
            await self.drain(runtime, session, DistanceReading(block=1, distance_mm=100, tof=1))
            remove()
            await self.drain(runtime, session, DistanceReading(block=2, distance_mm=200, tof=2))
        finally:
            await runtime.async_stop()
        self.assertEqual(seen, [1])
        self.assertEqual(runtime.distance_mm, 200)

    async def test_availability_follows_the_session_state(self):
        session = FakeSession(state=SessionState.BACKOFF)
        runtime = runtime_for(session)
        self.assertFalse(runtime.available)
        session.state = SessionState.READY_COMPATIBILITY
        self.assertTrue(runtime.available)

    async def test_stop_closes_the_session_and_cancels_its_tasks(self):
        session = FakeSession()
        runtime = runtime_for(session)
        await runtime.async_start()
        await runtime.async_stop()
        self.assertTrue(session.closed)
        self.assertTrue(session.maintained)

    async def test_stop_is_safe_before_any_observation_arrives(self):
        session = FakeSession()
        runtime = runtime_for(session)
        await runtime.async_start()
        await runtime.async_stop()
        self.assertIsNone(runtime.distance_mm)
        self.assertIsNone(runtime.last_access)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
@unittest.skipUnless(HOME_ASSISTANT, "requires the homeassistant package")
class ComponentSurfaceTests(unittest.IsolatedAsyncioTestCase):
    def test_every_module_imports(self):
        """A component that fails to import is a component Home Assistant drops."""

        for module in (
            "openaliro",
            "openaliro.config_flow",
            "openaliro.const",
            "openaliro.device_trigger",
            "openaliro.diagnostics",
            "openaliro.event",
            "openaliro.runtime",
            "openaliro.sensor",
        ):
            with self.subTest(module=module):
                __import__(module)

    def test_manifest_matches_the_domain(self):
        import json

        from openaliro.const import DOMAIN

        manifest = json.loads(
            (COMPONENT / "openaliro" / "manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["domain"], DOMAIN)
        self.assertIn("config_flow", manifest)

    async def test_diagnostics_expose_no_path_identity_or_console_text(self):
        from types import SimpleNamespace

        from openaliro.const import CONF_BAUD, CONF_DEVICE_ID, CONF_SERIAL_IDENTITY
        from openaliro.diagnostics import async_get_config_entry_diagnostics

        session = FakeSession()
        runtime = runtime_for(session)
        runtime.distance_mm = 548
        runtime.last_access = AccessEvent(verdict="granted")
        entry = SimpleNamespace(
            data={
                CONF_DEVICE_ID: "front-door",
                CONF_BAUD: 115200,
                CONF_SERIAL_IDENTITY: "0123456789abcdef01234567",
            },
            runtime_data=runtime,
        )

        report = await async_get_config_entry_diagnostics(None, entry)

        rendered = repr(report)
        self.assertNotIn("0123456789abcdef01234567", rendered)
        self.assertNotIn("private-path", rendered)
        self.assertNotIn("/dev/", rendered)
        self.assertTrue(report["config"]["serial_identity_configured"])
        self.assertEqual(report["runtime"]["last_access"], "granted")
        self.assertTrue(report["runtime"]["distance_available"])

    def test_device_triggers_are_limited_to_the_two_verdicts(self):
        from openaliro import device_trigger

        self.assertEqual(
            set(device_trigger.TRIGGER_TYPES), {"access_granted", "access_denied"}
        )


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=1).result.wasSuccessful() else 1)
