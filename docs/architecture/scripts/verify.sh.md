<!-- generated documentation — edit the source, not this file -->
# `scripts/verify.sh`

Pre-push sweep: every CI gate that a host can run, in one shot.
The point of this script is that "it passed locally" and "it will pass CI"
mean the same thing. Each row below is one CI *job* (not one workflow —
one job in ci.yml now runs all of them), running the same
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
It still gets a summary row saying it did not run: a gate that quietly
disappears from the sweep is the exact failure this script exists to prevent.
The PR runs it whenever the branch touches what it proves — which since the
path filter below is a narrower claim than this comment used to make, and the
reason WITH_CBMC=1 in CI is no longer the same as "on every pull request".
A gate whose tool is missing FAILS the sweep. It says so on its row, it is
counted apart from a hand-scoped SKIP=, and the run exits nonzero. Anything
softer is the original bug wearing a warning label: CI runs that gate whatever
this host has installed, so "could not check" has to read as "not verified",
not as "fine". `make tools-install` is the fix; SKIP="<gate>" is the override
for someone who has decided to accept the gap.
Most gates only read part of the tree, so most changes cannot break most of
them. A gate whose inputs this branch does not touch is skipped with a row
saying so — see the path-filter section below for how that is decided, and for
the four conditions that turn the whole thing off and sweep everything.
Env:
WITH_CBMC=1        also run the cbmc proof (off by default, see above)
SERIAL=1           one gate at a time, fail-fast, instead of lanes
SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
FILTER=0           run every gate whatever changed, ignoring the path filter
FILTER_BASE=<ref>  what "changed" is measured against. Unset means
origin/main; set-but-empty means there is no base, and
the filter is off.
COV_MIN=90         line-coverage floor. Reported, never blocking: under it the
row still passes and says so. Raise it to aim higher.
NO_COLOR=1         plain output (colour is the default, pipe or not)
FAIL_TAIL=40       lines of a failing gate's log to show inline

**discussed in** [`CONTRIBUTING.md`](../../../CONTRIBUTING.md), [`docs/troubleshooting.md`](../../troubleshooting.md), [`security/README.md`](../../../security/README.md)

## API

### `gate_paths()`
`scripts/verify.sh:235`

---- path filter ----------------------------------------------------------
What each gate reads, as git pathspecs. A gate is skipped when this branch
touches none of them. The bias is deliberately toward running: an EMPTY list
means "no filter, always run", so a gate added to the table above without a
row here keeps running rather than silently disappearing, which is the failure
mode this whole script exists to prevent. Widen a row when in doubt — an extra
minute of CI is cheap next to a gate that stopped watching its own inputs.
The always-run set is not a leftover. secrets, mal-diff and licenses read the
diff itself or the whole tree, so "which files changed" is their input rather
than a filter on it; docs and patch-drift are cheap and break from directions
their own paths do not predict (a moved anchor, a re-pinned upstream).

**called by** `gate_unchanged`

### `filter_touched()`
`scripts/verify.sh:320`

Changed paths matching a pathspec: committed since the merge base, plus
whatever is uncommitted or untracked right now. A pre-push sweep is asked
about the tree in front of it, not only about what is already committed.
Takes ONE argument: the whole space-separated pathspec list. It is split here,
with globbing off, rather than at the call site — `*.sh` is a pathspec for git
to interpret, and an unquoted expansion would let the shell match it against
the working directory first. Today nothing at the repository root ends in .sh
so it survives by luck; the day something does, the shellcheck gate would
quietly narrow to that one file. Splitting under `set -f` removes the luck.

**called by** `gate_unchanged`

### `gate_unchanged()`
`scripts/verify.sh:346`

0 = this branch touches nothing the gate reads, so it is skipped.

**called by** `run_gate`  ·  **calls** `filter_touched`, `gate_paths`

### `gate_need()`
`scripts/verify.sh:376`

What each gate needs on PATH. Empty = nothing beyond a shell and a compiler.
A bash-3.2 case function, not an associative array: macOS ships bash 3.2 and
tests/host/fuzz.sh already sets this precedent.

**called by** `run_gate`

### `gate_need_py()`
`scripts/verify.sh:413`

Python packages a gate's suites import. `command -v` cannot see these: they
are modules inside an interpreter, not binaries on PATH, which is exactly how
they went unnoticed. Absent, the suites still run and still report success,
having quietly skipped the checks that need them — ci.yml installs
both, so CI runs those checks whatever this host has.

**called by** `run_gate`

### `gate_label()`
`scripts/verify.sh:423`

Return the human-readable label for a CI gate name.
Labels are used in the summary row at the end of the verify sweep.

**called by** `gate_row`

### `gate_is_security()`
`scripts/verify.sh:487`

The gates that dispatch through scripts/security.sh. One list, because run_gate needs it too:
only this family uses an exit status of 2 to mean "this host cannot answer the question", and
reading that status the same way everywhere else would be wrong. docs is the example — it exits
2 for "your branch is behind origin/main", which is a real problem with an obvious fix, not a
gap in the host.

**called by** `gate_run`, `run_gate`

### `gate_run()`
`scripts/verify.sh:496`

The command each gate runs. Where CI runs a make target, so do we; where CI
runs a raw command, this reproduces it verbatim.

**called by** `run_gate`  ·  **calls** `gate_is_security`

### `gate_result()`
`scripts/verify.sh:655`

Write the result of a gate to a temporary file in RUNDIR and atomically rename
it, recording status (0 passed, 1 failed, 2 skipped), elapsed seconds, and an
optional reason string. Called from inside a lane subshell; the summary reads
these files after all lanes join. Losing this record must fail the lane: a
missing result can never be treated as a passing gate.

**called by** `run_gate`

### `gate_row()`
`scripts/verify.sh:670`

Prints the gate's row as it finishes. Concurrent lanes write these
interleaved, which is fine: each row is a single printf, and the summary
below is rebuilt from the .rc files rather than from what was printed.

**called by** `run_gate`  ·  **calls** `gate_label`

### `run_gate()`
`scripts/verify.sh:696`

0 passed, 1 failed, 2 did not run. Called from inside a lane subshell.

**called by** `run_lane`  ·  **calls** `gate_is_security`, `gate_need`, `gate_need_py`, `gate_result`, `gate_row`, `gate_run`, `gate_unchanged`

### `run_lane()`
`scripts/verify.sh:774`

One lane, in order. A failure stops the rest of that lane but not the others:
the gates sharing a lane share a build directory, so running the next one over
a half-built tree would only produce a second, confusing failure.

**calls** `run_gate`

### `why_notrun()`
`scripts/verify.sh:864`

Why a gate never started: its own lane stopped, or the tripwire did.
