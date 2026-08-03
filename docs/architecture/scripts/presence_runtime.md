<!-- generated documentation — edit the source, not this file -->
# `scripts/presence_runtime.py`

Build the minimal, deterministic presence runtime transfer archive.

## API

### `class BundleError(RuntimeError)`
`scripts/presence_runtime.py:38`

Exception raised when bundle construction fails.

**called by** `_runtime_sources`

### `_tarinfo(name: str, mode: int, kind: bytes, size: int=0)`
`scripts/presence_runtime.py:43`

Create a TarInfo record with deterministic metadata (zero UID/GID, empty user/group names, zero mtime) for tar.gz content. Caller specifies entry name, Unix mode, tarfile type constant, and optional size.

**called by** `build_bundle`

### `build_bundle(repo_root, output)`
`scripts/presence_runtime.py:75`

Write a deterministic tar.gz and return its SHA-256 hex digest.

**called by** `main`  ·  **calls** `_runtime_sources`, `_tarinfo`

### `build_parser(repo_root)`
`scripts/presence_runtime.py:127`

Build and return an argument parser for the presence-runtime archive tool with --output option.

**called by** `main`

<details><summary>Undocumented (2)</summary>

- `_runtime_sources`
- `main`

</details>
