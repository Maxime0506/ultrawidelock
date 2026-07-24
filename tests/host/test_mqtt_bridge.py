#!/usr/bin/env python3
"""Unit tests for integration/homeassistant/aliro_mqtt_bridge.py.

Stdlib only; run directly or via tests/host/run.sh. Everything here is a dry
path — paho-mqtt and pyserial are imported by the bridge only when publishing
to a broker or opening a real port, so neither is needed.

The range-line regex is drift-gated against the firmware format string in
modules/woz_uwb/src/ccc/ccc_shim_rx.c: a line rendered from that exact format
must parse. The ACCESS lines come from the fetched Nordic add-on (not in this
repo), so those are behavior pins only.
"""

import contextlib
import io
import json
import os
import re
import sys
import types
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "integration", "homeassistant"))

import aliro_mqtt_bridge as bridge  # noqa: E402

FW_SRC = os.path.join(ROOT, "modules", "woz_uwb", "src", "ccc", "ccc_shim_rx.c")

# A console line as Zephyr's console emits it: timestamp + level + module tag.
PREFIX = "[00:01:02.345,678] <inf> woz_uwb: "


class ParseLineTests(unittest.TestCase):
    def test_range_line(self):
        r = bridge.parse_line(PREFIX + "rng  blk=7   d=1234mm  tof=567")
        self.assertEqual(
            r, {"kind": "range", "block": 7, "distance_mm": 1234, "tof": 567}
        )

    def test_range_line_wide_block(self):
        # %-3u stops padding once the value fills the width
        r = bridge.parse_line(PREFIX + "rng  blk=123 d=88mm  tof=3")
        self.assertEqual(r["block"], 123)
        self.assertEqual(r["distance_mm"], 88)

    def test_range_negative_values(self):
        r = bridge.parse_line(PREFIX + "rng  blk=1   d=-12mm  tof=-4")
        self.assertEqual(r["distance_mm"], -12)
        self.assertEqual(r["tof"], -4)

    def test_access_granted(self):
        r = bridge.parse_line(PREFIX + "ACCESS GRANTED for credential 0")
        self.assertEqual(r, {"kind": "access", "verdict": "granted"})

    def test_access_denied(self):
        r = bridge.parse_line(PREFIX + "ACCESS DENIED (no valid credential)")
        self.assertEqual(r, {"kind": "access", "verdict": "denied"})

    def test_noise_ignored(self):
        for line in (
            "",
            PREFIX + "uwb ranging session started",
            PREFIX + "rng blk=x d=1mm tof=2",  # malformed block
            PREFIX + "access granted",  # verdicts are uppercase
        ):
            self.assertIsNone(bridge.parse_line(line), line)

    def test_firmware_format_drift(self):
        """A line rendered from the firmware's own format string must parse."""
        with open(FW_SRC, encoding="utf-8") as fh:
            src = fh.read()
        m = re.search(r'"(rng\s+blk=%-3u d=%dmm\s+tof=%d)"', src)
        self.assertIsNotNone(
            m, "range format string not found in ccc_shim_rx.c — update RNG_RE"
        )
        line = PREFIX + m.group(1).replace("%-3u", "%-3d") % (7, 1234, 567)
        r = bridge.parse_line(line)
        self.assertEqual(
            r, {"kind": "range", "block": 7, "distance_mm": 1234, "tof": 567}
        )


class PayloadTests(unittest.TestCase):
    def test_discovery_payloads(self):
        pairs = bridge.discovery_payloads("locknode")
        self.assertEqual(
            [t for t, _ in pairs],
            [
                "homeassistant/sensor/locknode/distance/config",
                "homeassistant/event/locknode/access/config",
            ],
        )
        dist, access = pairs[0][1], pairs[1][1]
        # both entities share one availability topic and one device
        self.assertEqual(dist["availability_topic"], "aliro/locknode/status")
        self.assertEqual(dist["availability_topic"], access["availability_topic"])
        self.assertEqual(dist["device"], access["device"])
        self.assertEqual(dist["unit_of_measurement"], "mm")
        self.assertEqual(access["event_types"], ["granted", "denied"])
        self.assertNotEqual(dist["unique_id"], access["unique_id"])
        for _, cfg in pairs:
            json.dumps(cfg)  # must be JSON-serializable as published

    def test_reading_to_message_range(self):
        topic, payload = bridge.reading_to_message(
            "locknode", {"kind": "range", "block": 3, "distance_mm": 950, "tof": 9}
        )
        self.assertEqual((topic, payload), ("aliro/locknode/distance", "950"))

    def test_reading_to_message_access(self):
        topic, payload = bridge.reading_to_message(
            "locknode", {"kind": "access", "verdict": "denied"}
        )
        self.assertEqual(topic, "aliro/locknode/access")
        self.assertEqual(json.loads(payload), {"event_type": "denied"})


class StreamTests(unittest.TestCase):
    def test_open_lines_stdin(self):
        old = sys.stdin
        sys.stdin = io.StringIO("one\ntwo\n")
        try:
            self.assertEqual(list(bridge.open_lines("-", 115200)), ["one\n", "two\n"])
        finally:
            sys.stdin = old

    def test_dry_run_end_to_end(self):
        log = (
            PREFIX + "boot banner noise\n"
            + PREFIX + "rng  blk=1   d=812mm  tof=44\n"
            + PREFIX + "ACCESS GRANTED for credential 2\n"
        )
        old_argv, old_stdin = sys.argv, sys.stdin
        sys.argv = ["aliro_mqtt_bridge.py", "--port", "-", "--dry-run", "--node", "tn"]
        sys.stdin = io.StringIO(log)
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                rc = bridge.main()
        finally:
            sys.argv, sys.stdin = old_argv, old_stdin
        self.assertEqual(rc, 0)
        lines = out.getvalue().splitlines()
        # 2 discovery announcements, then one message per matched line
        self.assertEqual(len(lines), 4)
        self.assertTrue(lines[0].startswith("homeassistant/sensor/tn/"))
        self.assertTrue(lines[1].startswith("homeassistant/event/tn/"))
        self.assertEqual(lines[2], "aliro/tn/distance 812")
        topic, _, payload = lines[3].partition(" ")
        self.assertEqual(topic, "aliro/tn/access")
        self.assertEqual(json.loads(payload), {"event_type": "granted"})


class FakeMqttClient:
    """Recording double for paho.mqtt.client.Client (imported lazily by main)."""

    def __init__(self):
        self.calls = []

    def will_set(self, topic, payload, retain=False):
        self.calls.append(("will", topic, payload, retain))

    def connect(self, host, port):
        self.calls.append(("connect", host, port))

    def loop_start(self):
        self.calls.append(("loop_start",))

    def loop_stop(self):
        self.calls.append(("loop_stop",))

    def disconnect(self):
        self.calls.append(("disconnect",))

    def publish(self, topic, payload=None, retain=False):
        self.calls.append(("publish", topic, payload, retain))


class BrokerPathTests(unittest.TestCase):
    """Drive main() without --dry-run against an injected fake paho module."""

    def run_main(self, stdin_obj):
        fake_client = FakeMqttClient()
        client_mod = types.ModuleType("paho.mqtt.client")
        client_mod.Client = lambda: fake_client
        mqtt_mod = types.ModuleType("paho.mqtt")
        mqtt_mod.client = client_mod
        paho_mod = types.ModuleType("paho")
        paho_mod.mqtt = mqtt_mod
        saved = {k: sys.modules.get(k) for k in ("paho", "paho.mqtt", "paho.mqtt.client")}
        sys.modules.update(
            {"paho": paho_mod, "paho.mqtt": mqtt_mod, "paho.mqtt.client": client_mod}
        )
        old_argv, old_stdin = sys.argv, sys.stdin
        sys.argv = ["aliro_mqtt_bridge.py", "--port", "-", "--node", "tn"]
        sys.stdin = stdin_obj
        try:
            rc = bridge.main()
        finally:
            sys.argv, sys.stdin = old_argv, old_stdin
            for k, v in saved.items():
                if v is None:
                    del sys.modules[k]
                else:
                    sys.modules[k] = v
        return rc, fake_client.calls

    def test_publish_flow(self):
        rc, calls = self.run_main(
            io.StringIO(PREFIX + "rng  blk=1   d=812mm  tof=44\n")
        )
        self.assertEqual(rc, 0)
        self.assertEqual(calls[0], ("will", "aliro/tn/status", "offline", True))
        self.assertEqual(calls[1], ("connect", "localhost", 1883))
        self.assertEqual(calls[2], ("loop_start",))
        pubs = [c for c in calls if c[0] == "publish"]
        # 2 retained discovery configs, online, the reading, offline
        self.assertEqual(len(pubs), 5)
        self.assertTrue(pubs[0][1].endswith("/distance/config") and pubs[0][3])
        self.assertEqual(pubs[2][1:], ("aliro/tn/status", "online", True))
        self.assertEqual(pubs[3][1:3], ("aliro/tn/distance", "812"))
        self.assertEqual(pubs[4][1:], ("aliro/tn/status", "offline", True))
        self.assertEqual(calls[-2:], [("loop_stop",), ("disconnect",)])

    def test_keyboard_interrupt_still_goes_offline(self):
        class Interrupting:
            def __iter__(self):
                yield PREFIX + "ACCESS DENIED\n"
                raise KeyboardInterrupt

        rc, calls = self.run_main(Interrupting())
        self.assertEqual(rc, 0)
        self.assertEqual(
            calls[-3], ("publish", "aliro/tn/status", "offline", True)
        )


class SerialPathTests(unittest.TestCase):
    def test_open_lines_serial(self):
        class FakeSerial:
            def __init__(self, port, baud, timeout=None):
                self.reads = iter([b"", b"rng one\n", b"rng two\n"])

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

            def readline(self):
                return next(self.reads, b"idle\n")

        serial_mod = types.ModuleType("serial")
        serial_mod.Serial = FakeSerial
        saved = sys.modules.get("serial")
        sys.modules["serial"] = serial_mod
        try:
            gen = bridge.open_lines("/dev/fake", 115200)
            self.assertEqual(next(gen), "rng one\n")  # empty read skipped
            self.assertEqual(next(gen), "rng two\n")
            gen.close()
        finally:
            if saved is None:
                del sys.modules["serial"]
            else:
                sys.modules["serial"] = saved


if __name__ == "__main__":
    unittest.main(verbosity=1)
