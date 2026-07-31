# Security scanning

What blocks a merge, what only reports, and what each tool is actually good for here.

The audit kit that produced this lives in `internal/security-audit/` (gitignored). This directory
is the part that runs on every change.

## Two lanes

| | `security.yml` | `security-deep.yml` |
|---|---|---|
| Runs on | every pull request and push to main | weekly, push to main, manual |
| Blocks a merge | **yes** | no |
| Wall time | ~60s (8 parallel jobs) | minutes |
| Reports via | failing the job | code-scanning alerts (SARIF) |

The split is the whole design. A gate that makes someone wait ten minutes to fix a typo gets
marked non-required within a month, and the fast checks sharing that workflow stop blocking with
it. So anything slow is in the lane that blocks nothing.

## The eight blocking gates

All eight run from `scripts/security.sh`, which is also what `make security` and the eight
security rows in `scripts/verify.sh` call. One implementation, so "it passed locally" and "it will pass CI"
keep meaning the same thing.

| Gate | Tool | Catches |
|---|---|---|
| `secrets` | gitleaks + `gitleaks.toml` | credentials, key material, bench identifiers, absolute home paths |
| `mal-diff` | `scripts/security-diff.sh` | binaries, `+x` bits, symlinks, gitlinks, capture files, oversized additions |
| `semgrep` | `semgrep-openaliro.yml`, `semgrep-malicious.yml`, registry packs | injection, unsafe parsing, crypto misuse, droppers, install hooks |
| `deps` | osv-scanner, pip-audit | known-vulnerable **and known-malicious** packages |
| `web` | `security-web.sh`, retire.js | CDN pins, subresource integrity, CSP, vulnerable vendored JS |
| `ct` | `security-ct.sh`, ctgrind/valgrind | **secret-dependent branches** — the only gate that can see a timing leak |
| `esp` | `security-workspace.sh esp` | ESP component ranges that resolve on the runner |
| `attest` | `security-attest.sh` | release provenance is configured before a tag needs it |

```sh
make security                      # all eight, ~30s
make security GATES="semgrep"      # one
make verify                        # all four, alongside every other CI gate
```

### ERROR blocks, WARNING reports

Only ERROR-severity semgrep findings fail a build. That is not timidity. Three rules in
`semgrep-openaliro.yml` are marked NOISY on purpose — `memcpy` from a wire length, `memset` on a
key buffer, an all-zero IV — because their false negatives are expensive enough to be worth their
false positives. There are 32 such findings today. Blocking on them would train everyone to
bypass the gate, and the ERROR rules would go with it.

Promoting a rule to ERROR means it has run clean over the whole tree. When
`openaliro-variable-time-compare-on-secret` was first run it produced four hits, three of them
false: two comparing a *public* key against a derived one, one comparing a CBOR map key by name.
The rule was tightened rather than accepted, and the real hit
(`modules/woz_aliro/src/aliro_stepup.c`) was fixed with a constant-time compare.

## What is deliberately not used

| Not used | Why |
|---|---|
| ClamAV, YARA | Signature AV over C source is near-zero signal and costs a ~300 MB database download per run. The real "malicious file" question here is structural, which is what `security-diff.sh` answers. |
| ZAP, DAST | There is no web application. The honest dynamic testing for this project is the existing `make fuzz`, `make test-san` and `make cbmc`, plus RF work on a bench. |
| syft / SBOM | An SBOM of this source tree lists `bun.lock` and `pyproject.toml` and misses NCS, Zephyr, ESP-IDF, esp-matter and the ESP component registry — most of the shipped binary. Publishing one that omits the RTOS and the TLS stack is worse than publishing none, because it looks like an answer. |
| trivy, grype | A second opinion on the lockfiles osv-scanner already reads. Checked while building this: both agree with osv-scanner on this repository, finding the identical two advisories and nothing more. |

The `syft / SBOM` objection above was answered rather than accepted: `security-workspace.sh sbom`
runs syft over `workspace/` **after** `make bootstrap`, so the SBOM includes NCS, Zephyr and the
ESP managed components. It runs in the deep lane because it needs a fetched tree.

## Known blind spots

Written down because a clean report over code nothing read is the failure this whole directory
exists to prevent.

1. **semgrep cannot parse 19 files** — listed in `semgrep-parse-baseline.txt`, mostly Zephyr and
   ESP-IDF macro forms. Those files are *not scanned by semgrep at all*. The `semgrep` gate fails
   if a twentieth appears, and the list names the four gates (clang-tidy, CodeQL, CBMC,
   libFuzzer) that do read them.
2. **CodeQL's C database is built from `make test`** — so it covers the host-compilable set in
   `tests/host/sources.sh`. Anything behind a Zephyr-only or ESP-IDF-only `#ifdef` is invisible
   to it.
3. **`web-flasher` still loads two unpinned chunks** — `install-button.js` is pinned by exact
   version and SRI, but it `import()`s two further chunks from unpkg at runtime, and dynamic
   imports carry no integrity metadata. They are constrained by the page's CSP origin list only.
   Closing this properly means vendoring the esp-web-tools dist tree.
4. **No SCA on the firmware dependencies** — Dependabot now watches `tools/tui` (npm) and
   `integration/homeassistant` (pip), but nothing watches the NCS tree, ESP-IDF, esp-matter, or
   the ESP component registry, because none is in the tree at scan time.

## Bumping a pinned tool

Versions and checksums live in `.github/workflows/security.yml`, and `scripts/toolchain.sh` reads
them from there rather than keeping a second copy. To bump one, change it in the workflow, then:

```sh
make tools          # reports any host tool now off the CI pin
make tools-install  # installs the pinned versions
```

## Adding a rule

1. Write it in `semgrep-openaliro.yml` (a bug we might write) or `semgrep-malicious.yml`
   (something that is not trying to be firmware).
2. Run `make security GATES=semgrep` over the whole tree.
3. If it fires anywhere, it is WARNING until those are fixed or the rule is tightened. Only a rule
   that runs clean may be ERROR.
4. Check it actually fires: plant the bug it is meant to catch in a scratch file and confirm. A
   rule that matches nothing looks identical to a rule that is silently broken — the C rules in
   `semgrep-openaliro.yml` were verified this way against a five-bug canary.
