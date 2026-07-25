<!-- generated documentation — edit the source, not this file -->
# `scripts/bootstrap.sh`

bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream.
Fetches everything the build needs from public GitHub into ./workspace
(git-ignored), then applies our integration patches on top. It never reads from
any other local checkout — a clean upstream fetch every time.
Fetches (all public):
- Nordic add-on  ncs-door-lock-and-access-control @ the pin below
- NCS v3.3.0 + Zephyr + every module (via the add-on's own west manifest)
The NCS v3.3.0 toolchain it needs is installed here too, once per machine, so
a clone reaches a build in one command instead of three.
Usage:  scripts/bootstrap.sh                       # workspace in ./workspace
ALIRO_WS=/big/disk/ws scripts/bootstrap.sh # put the multi-GB workspace elsewhere

**discussed in** [`docs/protocol-notes.md`](../../protocol-notes.md), [`docs/set-up.md`](../../set-up.md), [`ports/nrf5340dk/README.md`](../../../ports/nrf5340dk/README.md)

## API

### `apply_to()`
`scripts/bootstrap.sh:108`

Apply patch files to a repository, ensuring it is pristine (no uncommitted changes) before patching.

<details><summary>Undocumented (1)</summary>

- `launch`

</details>
