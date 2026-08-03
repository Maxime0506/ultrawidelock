#!/usr/bin/env python3
"""Build the minimal, deterministic presence runtime transfer archive."""

import argparse
import gzip
import hashlib
import io
import os
import pathlib
import stat
import sys
import tarfile
import tempfile

ARCHIVE_ROOT = "presence-runtime"

# The runtime import graph is deliberately explicit. Adding a source file here
# is a security-relevant packaging change and must update the membership test.
RUNTIME_FILES = (
    ("host/presence/presenced", 0o755),
    ("host/presence/presence-run", 0o755),
    ("host/presence/presence-enroll", 0o755),
    ("host/presence/presence_service.py", 0o644),
    ("host/presence/presence_client.py", 0o644),
    ("tools/presence_git.py", 0o644),
    ("tools/presence_verify.py", 0o644),
    ("tools/piv_pin.py", 0o755),
)

ARCHIVE_DIRECTORIES = (
    ARCHIVE_ROOT,
    f"{ARCHIVE_ROOT}/host",
    f"{ARCHIVE_ROOT}/host/presence",
    f"{ARCHIVE_ROOT}/tools",
)


class BundleError(RuntimeError):
    """Exception raised when bundle construction fails."""
    pass


def _tarinfo(name: str, mode: int, kind: bytes, size: int = 0):
    """Create a TarInfo record with deterministic metadata (zero UID/GID, empty user/group names, zero mtime) for tar.gz content. Caller specifies entry name, Unix mode, tarfile type constant, and optional size."""
    info = tarfile.TarInfo(name)
    info.type = kind
    info.mode = mode
    info.size = size
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    return info


def _runtime_sources(repo_root: pathlib.Path):
    for relative, expected_mode in RUNTIME_FILES:
        source = repo_root / relative
        try:
            source_stat = source.lstat()
        except FileNotFoundError as exc:
            raise BundleError(f"missing runtime source: {relative}") from exc
        if not stat.S_ISREG(source_stat.st_mode):
            raise BundleError(f"runtime source is not a regular file: {relative}")
        actual_mode = stat.S_IMODE(source_stat.st_mode) & 0o777
        if actual_mode != expected_mode:
            raise BundleError(
                f"runtime source mode drift for {relative}: "
                f"{actual_mode:04o}, expected {expected_mode:04o}"
            )
        yield relative, expected_mode, source.read_bytes()


def build_bundle(repo_root, output):
    """Write a deterministic tar.gz and return its SHA-256 hex digest."""
    repo_root = pathlib.Path(repo_root).resolve()
    output = pathlib.Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    sources = list(_runtime_sources(repo_root))

    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    os.close(fd)
    temporary = pathlib.Path(temporary_name)
    try:
        with temporary.open("wb") as raw:
            with gzip.GzipFile(
                filename="",
                mode="wb",
                compresslevel=9,
                fileobj=raw,
                mtime=0,
            ) as compressed:
                with tarfile.open(
                    fileobj=compressed,
                    mode="w",
                    format=tarfile.USTAR_FORMAT,
                ) as bundle:
                    for directory in ARCHIVE_DIRECTORIES:
                        bundle.addfile(
                            _tarinfo(directory, 0o755, tarfile.DIRTYPE)
                        )
                    for relative, mode, data in sources:
                        member = _tarinfo(
                            f"{ARCHIVE_ROOT}/{relative}",
                            mode,
                            tarfile.REGTYPE,
                            size=len(data),
                        )
                        bundle.addfile(member, io.BytesIO(data))
            raw.flush()
            os.fsync(raw.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, output)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise

    return hashlib.sha256(output.read_bytes()).hexdigest()


def build_parser(repo_root):
    """Build and return an argument parser for the presence-runtime archive tool with --output option."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--output",
        default=str(repo_root / "build" / "presence-runtime.tar.gz"),
        help="archive path (default: build/presence-runtime.tar.gz)",
    )
    return parser


def main(argv=None) -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    args = build_parser(repo_root).parse_args(argv)
    try:
        digest = build_bundle(repo_root, args.output)
    except (BundleError, OSError, tarfile.TarError) as exc:
        print(f"presence-runtime: {exc}", file=sys.stderr)
        return 1
    output = pathlib.Path(args.output)
    print(
        f"presence-runtime: built {output.name} "
        f"({output.stat().st_size} bytes, sha256 {digest})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
