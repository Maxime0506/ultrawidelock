#!/usr/bin/env python3
"""HA=1-only tests for the local custom-component archive builder."""

import importlib.util
import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "integration" / "homeassistant" / "tools" / "package_component.py"
SPEC = importlib.util.spec_from_file_location("package_component", SCRIPT)
assert SPEC and SPEC.loader
package_component = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_component)


@unittest.skipUnless(os.environ.get("HA") == "1", "requires explicit HA=1")
class PackageTests(unittest.TestCase):
    def test_archive_contains_component_and_one_vendored_shared_library(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            archive_path = package_component.build_archive(Path(temporary_directory) / "component.zip")
            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())
                manifest = json.loads(archive.read("custom_components/openaliro/manifest.json"))
        self.assertIn("custom_components/openaliro/manifest.json", names)
        self.assertIn("custom_components/openaliro/config_flow.py", names)
        self.assertIn("custom_components/openaliro/device_trigger.py", names)
        self.assertIn("custom_components/openaliro/diagnostics.py", names)
        self.assertIn("custom_components/openaliro/lib/openaliro_ha/serial_session.py", names)
        self.assertIn("pyserial>=3.5,<4", manifest["requirements"])
        self.assertFalse(any("__pycache__" in name or name.endswith(".pyc") for name in names))
