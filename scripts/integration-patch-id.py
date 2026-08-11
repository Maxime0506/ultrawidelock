#!/usr/bin/env python3
"""Identify the exact integration patch set and selected HA mode."""

import hashlib
import sys
import tempfile
from pathlib import Path


def patch_id(patch_dir: Path, ha_mode: str) -> str:
    patches = sorted(patch_dir.glob("*.patch"))
    if not patches:
        raise ValueError(f"no patch files in {patch_dir}")

    digest = hashlib.sha256(b"ultrawidelock-integration-patches-v1\0")
    digest.update(f"ha={ha_mode}\0".encode())
    for patch in patches:
        name = patch.name.encode()
        data = patch.read_bytes()
        digest.update(len(name).to_bytes(4, "big"))
        digest.update(name)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest()


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="ultrawidelock-patch-id-") as work:
        root = Path(work)
        (root / "b.patch").write_text("second\n", encoding="utf-8")
        (root / "a.patch").write_text("first\n", encoding="utf-8")
        initial = patch_id(root, "0")
        assert initial == patch_id(root, "0")
        print("  ok   patch ID is deterministic")

        (root / "a.patch").write_text("changed\n", encoding="utf-8")
        assert initial != patch_id(root, "0")
        print("  ok   patch ID changes with content")

        assert patch_id(root, "0") != patch_id(root, "1")
        print("  ok   patch ID changes with HA mode")
    print("integration patch ID: PASS")


def main() -> None:
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return
    if len(sys.argv) != 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(f"usage: {sys.argv[0]} PATCH_DIR HA_MODE")
    print(patch_id(Path(sys.argv[1]), sys.argv[2]))


if __name__ == "__main__":
    main()
