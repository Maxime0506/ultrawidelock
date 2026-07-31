<!-- generated documentation — edit the source, not this file -->
# `scripts/security.sh`

security.sh — the four fast security gates, in one place.
CI (.github/workflows/security.yml), `make security` and the `secrets`/`mal-diff`/`semgrep`/
`deps` rows in scripts/verify.sh all call THIS file. That is the point of it: the repo already
learned once that a gate reproduced by hand in two places drifts in one of them, which is why
verify.sh's header insists on running the same command CI runs. Here there is only one command.
scripts/security.sh              # all four gates, in order
scripts/security.sh semgrep      # one gate
make security                    # same thing, through the front door
Gates:
secrets    gitleaks over the tree, or over a commit range when one is given
mal-diff   scripts/security-diff.sh, the structural malicious-change checks
semgrep    security/*.yml plus the pinned registry packs; ERROR blocks, WARNING reports
deps       osv-scanner on the bun lockfile, pip-audit on the Home Assistant dependencies
Exit 0 if every gate selected passed, 1 otherwise. A gate whose tool is missing FAILS rather
than skipping, for the reason verify.sh gives at length: CI runs it whatever this host has, so
"could not check" has to read as "not verified", never as "fine".
Env:
SECURITY_BASE / SECURITY_HEAD   commit range; CI passes the PR's base and head
SEMGREP_NO_REGISTRY=1           local rulesets only, skipping the network fetch
NO_COLOR=1                      plain output

**discussed in** [`security/README.md`](../../../security/README.md)

## API

### `gate_secrets()`
`scripts/security.sh:60`

---- secrets ---------------------------------------------------------------
Two scopes on purpose. With a range, only the commits being proposed are scanned, which is what
a pull request needs and costs about two seconds. Without one, the whole working tree is
scanned. Neither is the full-history scan — that lives in the weekly deep lane, because at ~18s
over 576 commits it is too slow to sit in front of every push and its answer changes only when
history is rewritten.

**called by** `run_one`  ·  **calls** `have`, `hdr`, `missing`

### `gate_maldiff()`
`scripts/security.sh:84`

---- mal-diff --------------------------------------------------------------

**called by** `run_one`  ·  **calls** `hdr`

### `gate_semgrep()`
`scripts/security.sh:102`

---- semgrep ---------------------------------------------------------------
One invocation with every config, not one per ruleset: semgrep parses each target file once and
runs all loaded rules against it, so three configs in one run cost far less than three runs.
The severity split is the whole contract with contributors. ERROR fails; WARNING is printed and
does not. That is not timidity — three of this repo's WARNING rules (memcpy-from-a-wire-length,
memset-on-a-key-buffer, all-zero-IV) are documented NOISY in security/semgrep-openaliro.yml
because their false negatives are expensive enough to be worth their false positives. Blocking
on them would train everyone to bypass the gate, taking the ERROR rules with it.

**called by** `run_one`  ·  **calls** `have`, `hdr`, `missing`

### `gate_deps()`
`scripts/security.sh:234`

---- deps ------------------------------------------------------------------
osv-scanner is pointed at the lockfile rather than told to walk the tree. The walk resolves its
root oddly under a sandboxed shell and silently reports "no package sources found" — a clean
pass that scanned nothing. Naming the file cannot fail that way.
osv-scanner is also the malicious-package half of this gate: OSV carries the OpenSSF Malicious
Packages feed as MAL- advisories, so a dependency that is not merely vulnerable but hostile
comes back from the same query.

**called by** `run_one`  ·  **calls** `have`, `hdr`, `missing`

### `gate_web()`
`scripts/security.sh:303`

---- gates that live in their own script -----------------------------------
Each is big enough to want its own file (the web gate parses HTML, the ct gate compiles and
runs a harness under valgrind), but they dispatch through here so there is still one entry
point that CI, `make security` and verify.sh all share.

**called by** `run_one`

### `gate_ct()`
`scripts/security.sh:312`

ct is the one gate that can report neither pass nor fail. There is no valgrind for
darwin/arm64, so on the primary dev machine the honest answer is "not checked here" — exit 2,
which verify.sh renders as a skip-host row rather than as a pass. Not skip-tool: that one is
fatal to the sweep because `make tools-install` is the fix, and here there is nothing to
install. CI runs linux and never sees it.

**called by** `run_one`

### `run_one()`
`scripts/security.sh:320`

---- dispatch --------------------------------------------------------------

**calls** `gate_attest`, `gate_ct`, `gate_deps`, `gate_esp`, `gate_maldiff`, `gate_secrets`, `gate_semgrep`, `gate_web`

<details><summary>Undocumented (5)</summary>

- `have`
- `hdr`
- `missing`
- `gate_esp`
- `gate_attest`

</details>
