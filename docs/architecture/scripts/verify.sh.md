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
Gates are ordered cheapest-first and the run stops at the first failure, so a
one-second formatting slip never costs the eighty-six seconds of cbmc.
A gate whose tool is missing is SKIPPED LOUDLY: it says so on its row, it is
counted in the summary, and the final line names it. It never silently passes,
because CI will still run it.
Env:
SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
COV_MIN=90         line-coverage floor, matching host-tests.yml
NO_COLOR=1         plain output

## API

### `gate_need()`
`scripts/verify.sh:78`

What each gate needs on PATH. Empty = nothing beyond a shell and a compiler.
A bash-3.2 case function, not an associative array: macOS ships bash 3.2 and
tests/host/fuzz.sh already sets this precedent.

### `gate_run()`
`scripts/verify.sh:119`

The command each gate runs. Where CI runs a make target, so do we; where CI
runs a raw command, this reproduces it verbatim.

<details><summary>Undocumented (1)</summary>

- `gate_label`

</details>
