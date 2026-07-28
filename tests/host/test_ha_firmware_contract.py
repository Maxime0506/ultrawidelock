#!/usr/bin/env python3
"""HA=1-only drift test: the ESP32 firmware's discovery payloads against the agent's.

The firmware reimplements `discovery_payloads()` in C, so the two can drift apart
silently and only a Home Assistant install would notice. Rather than restating the
JSON here — a copy that rots the moment either side moves — this extracts the
format strings from ha_mqtt.c itself, compiles them, and diffs the rendered output
against what the agent publishes. Same approach as
test_dist_diagnostic_format_drift in test_ha_parser.py: render from the firmware's
own source, never from a literal.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "integration" / "homeassistant" / "src"))

from openaliro_ha.mqtt import discovery_payloads  # noqa: E402

FIRMWARE = ROOT / "ports" / "esp32" / "apps" / "matter-lock" / "main" / "ha_mqtt.c"
NODE = "front-door"
MODEL = "ESP32-S3 Aliro lock"

HARNESS = """#include <stdio.h>
#include <string.h>
#define HA_MQTT_MODEL "%(model)s"
static struct { char node[33]; } s_cfg;
%(macro)s
%(functions)s
int main(void) {
    char buf[640];
    strcpy(s_cfg.node, "%(node)s");
    discovery_distance_payload(buf, sizeof buf); puts(buf);
    discovery_access_payload(buf, sizeof buf); puts(buf);
    return 0;
}
"""


def _extract(source: str) -> tuple[str, str]:
    """Return the device-block macro and both payload builders, verbatim."""

    macro = re.search(r"#define HA_DEVICE_BLOCK_FMT.*?\n\n", source, re.S)
    functions = re.search(
        r"static int discovery_distance_payload.*?\n}\n\n"
        r"static int discovery_access_payload.*?\n}\n",
        source,
        re.S,
    )
    if macro is None or functions is None:
        raise AssertionError("ha_mqtt.c no longer shapes its discovery payloads as expected")
    return macro.group(0), functions.group(0)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class FirmwareDiscoveryContractTests(unittest.TestCase):
    def test_firmware_renders_the_agent_discovery_payloads(self):
        compiler = shutil.which("cc") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no host C compiler to render the firmware format strings")

        macro, functions = _extract(FIRMWARE.read_text())
        with tempfile.TemporaryDirectory() as work:
            source = Path(work) / "discovery.c"
            binary = Path(work) / "discovery"
            source.write_text(
                HARNESS % {"model": MODEL, "macro": macro, "functions": functions, "node": NODE}
            )
            subprocess.run([compiler, "-o", str(binary), str(source)], check=True)
            rendered = subprocess.run(
                [str(binary)], capture_output=True, text=True, check=True
            ).stdout.splitlines()

        expected = [json.dumps(payload) for _, payload in discovery_payloads(NODE, MODEL)]
        self.assertEqual(rendered, expected)


if __name__ == "__main__":
    unittest.main()
