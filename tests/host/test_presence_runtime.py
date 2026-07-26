#!/usr/bin/env python3
"""Packaging tests for the seven-file presence runtime bundle."""

import ast
import hashlib
import os
import pathlib
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import presence_runtime as runtime  # noqa: E402


EXPECTED = {
    "host/presence/presenced": 0o755,
    "host/presence/presence-run": 0o755,
    "host/presence/presence-enroll": 0o755,
    "host/presence/presence_service.py": 0o644,
    "host/presence/presence_client.py": 0o644,
    "tools/presence_git.py": 0o644,
    "tools/presence_verify.py": 0o644,
}


def extract_regular_files(archive, destination):
    """Extract this known-safe bundle without relying on tarfile.extractall."""
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle.getmembers():
            relative = pathlib.PurePosixPath(member.name)
            if relative.is_absolute() or ".." in relative.parts:
                raise AssertionError(f"unsafe archive member: {member.name}")
            target = destination.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                target.chmod(member.mode)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                source = bundle.extractfile(member)
                target.write_bytes(source.read())
                target.chmod(member.mode)
            else:
                raise AssertionError(f"unexpected archive member type: {member.name}")


class PresenceRuntimeTests(unittest.TestCase):
    def build(self, directory, name="presence-runtime.tar.gz"):
        output = pathlib.Path(directory, name)
        runtime.build_bundle(ROOT, output)
        return output

    def test_manifest_is_exactly_the_seven_runtime_files(self):
        self.assertEqual(dict(runtime.RUNTIME_FILES), EXPECTED)

    def test_archive_contains_only_expected_files_with_fixed_safe_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.build(directory)
            with tarfile.open(archive, "r:gz") as bundle:
                members = bundle.getmembers()

        files = {
            member.name.removeprefix(runtime.ARCHIVE_ROOT + "/"): member
            for member in members
            if member.isfile()
        }
        self.assertEqual(set(files), set(EXPECTED))
        for relative, member in files.items():
            with self.subTest(path=relative):
                self.assertEqual(member.mode, EXPECTED[relative])
                self.assertEqual(member.uid, 0)
                self.assertEqual(member.gid, 0)
                self.assertEqual(member.mtime, 0)
                self.assertFalse(member.issym())
                self.assertFalse(member.islnk())

        self.assertNotIn(".presence/enrolled", files)
        self.assertNotIn("host/presence/README.md", files)

    def test_archived_bytes_match_sources_exactly(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.build(directory)
            with tarfile.open(archive, "r:gz") as bundle:
                for relative in EXPECTED:
                    member = bundle.getmember(f"{runtime.ARCHIVE_ROOT}/{relative}")
                    archived = bundle.extractfile(member).read()
                    self.assertEqual(archived, (ROOT / relative).read_bytes(), relative)

    def test_repeated_builds_are_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            first = self.build(directory, "first.tar.gz")
            second = self.build(directory, "second.tar.gz")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(
                hashlib.sha256(first.read_bytes()).digest(),
                hashlib.sha256(second.read_bytes()).digest(),
            )

    def test_runtime_annotations_are_safe_for_python_39(self):
        for relative in (
            "host/presence/presence_service.py",
            "host/presence/presence_client.py",
        ):
            with self.subTest(path=relative):
                source = (ROOT / relative).read_text(encoding="utf-8")
                tree = ast.parse(source, filename=relative, feature_version=(3, 9))
                futures = [
                    node
                    for node in tree.body
                    if isinstance(node, ast.ImportFrom) and node.module == "__future__"
                ]
                self.assertTrue(
                    any(
                        any(name.name == "annotations" for name in node.names)
                        for node in futures
                    ),
                    f"{relative} must defer annotation evaluation",
                )

    def test_extracted_entry_points_run_in_isolation(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.build(directory)
            extracted = pathlib.Path(directory, "extracted")
            extract_regular_files(archive, extracted)
            root = extracted / runtime.ARCHIVE_ROOT

            for entry in ("presenced", "presence-run", "presence-enroll"):
                with self.subTest(entry=entry):
                    executable = root / "host" / "presence" / entry
                    self.assertTrue(executable.stat().st_mode & stat.S_IXUSR)
                    result = subprocess.run(
                        [str(executable), "--help"],
                        cwd=root,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("usage:", result.stdout)

    def test_extracted_entry_points_import_with_macos_system_python(self):
        system_python = pathlib.Path("/usr/bin/python3")
        if not system_python.exists():
            self.skipTest("macOS system Python is unavailable")

        with tempfile.TemporaryDirectory() as directory:
            archive = self.build(directory)
            extracted = pathlib.Path(directory, "extracted")
            extract_regular_files(archive, extracted)
            root = extracted / runtime.ARCHIVE_ROOT

            for entry in ("presenced", "presence-run", "presence-enroll"):
                with self.subTest(entry=entry):
                    result = subprocess.run(
                        [
                            str(system_python),
                            str(root / "host" / "presence" / entry),
                            "--help",
                        ],
                        cwd=root,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("usage:", result.stdout)


if __name__ == "__main__":
    unittest.main()
