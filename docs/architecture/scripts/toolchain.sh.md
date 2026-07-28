<!-- generated documentation — edit the source, not this file -->
# `scripts/toolchain.sh`

toolchain.sh — what the CI gates need, whether this host has it, how to get it.
`make verify` runs eighteen CI gates and skips loudly when a gate's tool is
absent. Skipping loudly is honest, but it leaves the reader to work out what
to install, from where, and at which version. That is this script: one
manifest, two modes.
scripts/toolchain.sh            report every tool, its gate, and its status
scripts/toolchain.sh install    install the missing ones, after confirming
Nothing is installed without being printed first and agreed to. `install`
shows the exact command list and waits for a y; -y answers it in advance for
unattended use.
Versions matter for four of these. clang-format and clang-tidy change their
output between releases, so a host one version off the CI pin fails a gate
that CI passes (or worse, the reverse). Those rows carry the pin CI uses and
say so when the host disagrees.
Out of scope, same boundary as verify.sh: the firmware toolchains. NCS (~6.5
GB, `make bootstrap`) and ESP-IDF are per-target installs with their own
documented procedures — see docs/set-up.md. This covers the host gates only.
Adding a gate to verify.sh without adding its tool here is caught: `check`
reads verify.sh's own gate_need + gate_need_py tables and fails on any name it
cannot explain, and fails again if either table stops parsing. What it does
NOT catch is a row here that no gate needs any more, and none of it runs in
CI — only when someone runs `make tools`.
Env:
ASSUME_YES=1   same as `install -y`
NO_COLOR=1     plain output

## API

### `pipx_or_pip()`
`scripts/toolchain.sh:106`

--force because these commands are only ever emitted for a tool that is
missing or sitting on the wrong version, and plain `pipx install` declines to
touch a package it has already installed — which is exactly the repin case.

**called by** `tool_install`

### `tool_gate()`
`scripts/toolchain.sh:138`

Which gate stops working without it. This is the "why do I need this" column,
and it is the reason a row exists at all.

### `tool_pin()`
`scripts/toolchain.sh:178`

Extract the pinned version string for a tool (clang-format, clang-tidy, zizmor, reuse, actionlint, emcc, or markdown) from the corresponding CI workflow file. Returns the version or empty string if not found or tool name is unrecognized.

**called by** `tool_install`, `tool_note`

### `actionlint_url()`
`scripts/toolchain.sh:198`

actionlint's Linux install is CI's own: a release tarball checked against a
sha256. Both come out of the workflow for the same reason the pins do.

**called by** `tool_install`

### `actionlint_sha()`
`scripts/toolchain.sh:203`

Extract the actionlint binary hash (64 hex characters) from workflow-lint.yml, return it or empty string if not found.

**called by** `tool_install`

### `tool_probe()`
`scripts/toolchain.sh:213`

Present on this host? Echoes the version (or a bare "installed") and returns
0; returns 1 when absent. Three rows are not a plain `command -v`:
llvm-cov  macOS keeps it inside the Xcode SDK, reachable only via xcrun —
which is how tests/host/coverage.sh calls it.
emcc      twin-wasm.sh sources ~/emsdk/emsdk_env.sh when emcc is off PATH.
markdown  a python import, not a binary.

### `tool_install()`
`scripts/toolchain.sh:271`

The command that installs it here. Empty = this host has no packaged route and
the row prints a pointer instead.

**calls** `actionlint_sha`, `actionlint_url`, `pipx_or_pip`, `tool_pin`

### `tool_note()`
`scripts/toolchain.sh:432`

Printed under a row that has no install command on this host.

**calls** `tool_pin`

### `version_of()`
`scripts/toolchain.sh:442`

Pull the leading dotted number out of a --version line, for the pin compare.

### `verify_needs()`
`scripts/toolchain.sh:452`

---- drift check: every gate tool verify.sh names must have a row here -----
verify.sh's gate_need() and gate_need_py() are the authority on what the gates
need. Reading them rather than restating them means a new gate cannot quietly
arrive without an install route: this check fails until someone adds the row.
Both functions, not just the first. gate_need_py() arrived later and covering
only gate_need() would have left python dependencies drifting freely, which is
the exact hole this check exists to close.
