#!/usr/bin/env python3
"""Build a local OpenAliro custom-component archive without publishing it."""

from __future__ import annotations

import argparse
import shutil
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "integration" / "homeassistant" / "custom_components" / "openaliro"
LIBRARY = ROOT / "integration" / "homeassistant" / "src" / "openaliro_ha"


def _copy_tree(source: Path, destination: Path) -> None:
    """Recursively copy source directory to destination, excluding __pycache__, *.pyc, *.pyo."""
    shutil.copytree(
        source,
        destination,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
    )


def build_archive(output: Path) -> Path:
    """Create an archive rooted at ``custom_components/openaliro``."""

    if not (COMPONENT / "manifest.json").is_file() or not LIBRARY.is_dir():
        raise RuntimeError("Home Assistant component sources are incomplete")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="openaliro-ha-package-") as temporary:
        staging = Path(temporary) / "custom_components" / "openaliro"
        staging.parent.mkdir(parents=True)
        _copy_tree(COMPONENT, staging)
        _copy_tree(LIBRARY, staging / "lib" / "openaliro_ha")
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(staging.parent.parent.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(staging.parent.parent))
    return output


def main() -> int:
    """Parse command-line arguments and build the Home Assistant component archive."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "openaliro-ha-component.zip")
    arguments = parser.parse_args()
    print(build_archive(arguments.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
