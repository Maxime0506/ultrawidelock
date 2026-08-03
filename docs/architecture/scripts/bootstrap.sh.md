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

### `launch()`
`scripts/bootstrap.sh:37`

Execute a command inside the nRF Connect SDK toolchain environment for NCS_VER, forwarding all arguments.
Wrapper around `nrfutil sdk-manager toolchain launch`.

### `apply_to()`
`scripts/bootstrap.sh:134`

Apply patch files to a repository, resetting it to its pinned HEAD first.
That reset is what makes bootstrap idempotent -- the previous run's patches have
to come off before this run's go on -- but hand-editing $WS is the normal way
upstream gets debugged here, and those edits look identical to it. So say what is
about to go, and keep a copy: a run that silently eats an afternoon of debugging
is the worst thing this script can do. ALIRO_KEEP_WS_EDITS=1 stops instead, for
when the edits are the point and re-patching is not.
