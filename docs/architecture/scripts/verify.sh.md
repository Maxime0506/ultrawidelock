<!-- generated documentation — edit the source, not this file -->
# `scripts/verify.sh`

Pre-push sweep: every CI gate that a host can run, in one shot.
The point of this script is that "it passed locally" and "it will pass CI"
mean the same thing. Each row below is one CI *job* (not one workflow —
tooling.yml and workflow-lint.yml each contribute several), running the same
command that job runs. Adding a job to .github/workflows/ without adding it
here re-opens the gap this script exists to close.
Out of scope, deliberately: firmware-builds.yml and release.yml. They need
ESP-IDF and NCS (~6.5 GB of toolchain) and take tens of minutes — not a push
gate. `make build` covers them once the toolchain is bootstrapped.
The gates run in lanes, several at once, because serially they are ~83s of
work on a machine with eight cores. A short serial tripwire goes first, so a
formatting slip still stops the sweep about four seconds in; then the
expensive gates run together and the sweep costs its slowest lane rather than
the sum of all of them. Measured back to back on an idle host: 83s serial,
34s in lanes, and 72s in lanes with cbmc on against 147s serial.
SERIAL=1 puts it back to one gate at a time, for a busy machine or for reading
a confusing failure in order.
One gate does not run by default: cbmc. At 64s it is twice the rest of the
sweep put together, spent on the gate whose input moves least — the wire
parsers it proves have been stable for months, and the fuzz gate exercises the
same code every run. WITH_CBMC=1 turns it on, taking the sweep to ~72s.
It still gets a summary row saying it did not run. cbmc.yml has no path
filter, so the PR runs it whatever happened here; a gate that quietly
disappears from the sweep is the exact failure this script exists to prevent.
A gate whose tool is missing FAILS the sweep. It says so on its row, it is
counted apart from a hand-scoped SKIP=, and the run exits nonzero. Anything
softer is the original bug wearing a warning label: CI runs that gate whatever
this host has installed, so "could not check" has to read as "not verified",
not as "fine". `make tools-install` is the fix; SKIP="<gate>" is the override
for someone who has decided to accept the gap.
Env:
WITH_CBMC=1        also run the cbmc proof (off by default, see above)
SERIAL=1           one gate at a time, fail-fast, instead of lanes
SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
COV_MIN=90         line-coverage floor, matching host-tests.yml
NO_COLOR=1         plain output

## API

### `gate_need()`
`scripts/verify.sh:162`

What each gate needs on PATH. Empty = nothing beyond a shell and a compiler.
A bash-3.2 case function, not an associative array: macOS ships bash 3.2 and
tests/host/fuzz.sh already sets this precedent.

**called by** `run_gate`

### `gate_need_py()`
`scripts/verify.sh:184`

Python packages a gate's suites import. `command -v` cannot see these: they
are modules inside an interpreter, not binaries on PATH, which is exactly how
they went unnoticed. Absent, the suites still run and still report success,
having quietly skipped the checks that need them — host-tests.yml installs
both, so CI runs those checks whatever this host has.

**called by** `run_gate`

### `gate_run()`
`scripts/verify.sh:216`

The command each gate runs. Where CI runs a make target, so do we; where CI
runs a raw command, this reproduces it verbatim.

**called by** `run_gate`

### `gate_row()`
`scripts/verify.sh:317`

Prints the gate's row as it finishes. Concurrent lanes write these
interleaved, which is fine: each row is a single printf, and the summary
below is rebuilt from the .rc files rather than from what was printed.

**called by** `run_gate`  ·  **calls** `gate_label`

### `run_gate()`
`scripts/verify.sh:333`

0 passed, 1 failed, 2 did not run. Called from inside a lane subshell.

**called by** `run_lane`  ·  **calls** `gate_need`, `gate_need_py`, `gate_result`, `gate_row`, `gate_run`

### `run_lane()`
`scripts/verify.sh:390`

One lane, in order. A failure stops the rest of that lane but not the others:
the gates sharing a lane share a build directory, so running the next one over
a half-built tree would only produce a second, confusing failure.

**calls** `run_gate`

### `why_notrun()`
`scripts/verify.sh:461`

Why a gate never started: its own lane stopped, or the tripwire did.

<details><summary>Undocumented (2)</summary>

- `gate_label`
- `gate_result`

</details>
