# mk/host.mk — everything that runs on this machine: the host suites, the
# security gates and the one-shot pre-push sweep. No NCS toolchain, no ESP-IDF,
# no hardware. Output lands under build/host.
#
# scripts/verify.sh calls several of these by name, so renaming one means
# editing that file too (tests/tooling/verify_test.sh will say so).

.PHONY: test test-san coverage fuzz cbmc check test-port test-ws test-verify \
        test-web twin-wasm test-twin verify \
        security security-web security-ct security-workspace security-fw security-attest

##@ Test
## test: run the host test suite for our logic  (no NCS toolchain / hardware)
test:
	@$(REPO_ROOT)/tests/host/run.sh

## test-san: host suite rebuilt under ASan + UBSan  ·  memory-bug gate
test-san:
	@SAN=1 $(REPO_ROOT)/tests/host/run.sh

## coverage: line coverage of every host suite, 0% rows for the rest  ->  table + HTML
##   Instrumented (clang source-based coverage); slower than `make test` and
##   rebuilt at -O0. Artifacts under build/host/coverage/ (html/index.html).
coverage:
	@$(REPO_ROOT)/tests/host/coverage.sh

## fuzz: fuzz the wire-facing parsers  ·  parser-hardening gate
##   Coverage-guided libFuzzer where available (CI), else a portable corpus
##   replay under ASan/UBSan (Apple clang ships no libFuzzer). Env: FUZZ_SECONDS=N,
##   FUZZ_STANDALONE=1. Args (via bash) restrict to named targets.
fuzz:
	@$(REPO_ROOT)/tests/host/fuzz.sh

## cbmc: bounded model-check the wire parsers  ·  memory-safety proof
##   Proves no out-of-bounds / bad-pointer access for all inputs up to each
##   harness bound. Needs cbmc on PATH (brew install cbmc / apt install cbmc).
cbmc:
	@$(REPO_ROOT)/tests/host/cbmc.sh

## check: every host-side suite under one banner  ->  live rows + summary table
##   Parallel by default; SERIAL=1 streams suites one at a time, SUITES="..."
##   scopes (firmware shared webtwin). The pre-push look, runnable any time.
check:
	@$(REPO_ROOT)/scripts/test-runner.sh

## test-port: host-runnable ESP32 port tests (port headers, crypto KATs, codec)
##   No ESP-IDF needed; the on-target build check inside skips cleanly without it.
test-port:
	@$(REPO_ROOT)/ports/esp32/test/run.sh

## test-ws: hermetic tests for per-worktree workspace auto-seeding
##   Runs in a temp dir with a stub bootstrap — no west, no hardware, and it
##   never touches this repo's own workspace/ or build/.
test-ws:
	@$(REPO_ROOT)/tests/tooling/ws_seed_test.sh

## test-verify: tests for the gates themselves  ·  make verify's own gate
##   Two files. verify_test.sh has two halves — static: the gate table still
##   covers every job in .github/workflows/, so a new CI job cannot be added
##   without either a local gate or a written reason; behavioral: a copy of
##   verify.sh run against stub tools in a temp git repo, checking that a missing
##   tool, a failed tripwire and an unmet coverage floor each fail the sweep
##   rather than passing quietly. security_diff_test.sh plants a binary, a mode
##   change, a symlink and a gitlink in a throwaway repo and checks the
##   malicious-change gate blocks each one — none of which can be asserted by
##   reading the script.
##   Nothing real is compiled; both files run in a couple of seconds.
test-verify:
	@$(REPO_ROOT)/tests/tooling/verify_test.sh
	@$(REPO_ROOT)/tests/tooling/security_diff_test.sh

## test-web: drift-gate the web-twin page against the firmware it cites
##   Re-reads every constant web-twin/index.html cites (file:line) from the C
##   tree and fails if a value moved. The decision logic itself is the real
##   firmware compiled to WASM (twin.js) — this guards the residual JS-side
##   constants (the ESP32 walk-up controller port + world pacing).
test-web:
	@python3 $(REPO_ROOT)/web-twin/check_constants.py

## twin-wasm: compile the twin's firmware to WASM  ->  web-twin/twin.js
##   modules/woz_uwb + the tests/host shim under Emscripten (needs emsdk on
##   PATH or in ~/emsdk). Reproducible: the committed twin.js is rebuilt and
##   byte-diffed by CI, so the page can never run stale firmware.
twin-wasm:
	@$(REPO_ROOT)/scripts/twin-wasm.sh

## test-twin: rebuild the WASM twin, then replay the test_twin.c scenario in node
test-twin: twin-wasm
	@node $(REPO_ROOT)/web-twin/selftest.cjs

## verify: run every host-runnable CI gate in one shot  ·  pre-push sweep
##   The 22 CI jobs a host can run — format, shellcheck, clang-tidy, fuzz, test,
##   twin-wasm, patch-drift, docs, test-san, test-port, test-ws, test-verify,
##   coverage (with the 90% floor), zizmor, licences, the four security gates,
##   cbmc — run in parallel lanes behind a serial tripwire, so a 1s formatting
##   slip stops it at once. A gate whose tool is missing
##   FAILS the sweep (`make tools-install` fixes it), because CI runs that gate
##   regardless. cbmc is the exception: 64s on its own, twice the rest of the
##   sweep, so it is off unless WITH_CBMC=1 (~72s), and its row says so.
##   The semgrep gate fetches its registry rule packs, so the sweep now wants
##   network; SEMGREP_NO_REGISTRY=1 drops to the in-tree rules only.
##   Builds no firmware, not even in a shell with ESP-IDF sourced (test-port's
##   target-build layer is held off, as it is on CI's runner). firmware-builds
##   and release stay out: ESP-IDF + NCS, tens of minutes.
##   Options: WITH_CBMC=1 adds the proof  ·  SKIP="fuzz docs" drops named gates
##            SERIAL=1 one gate at a time  ·  COV_MIN=90 coverage floor
verify:
	@$(REPO_ROOT)/scripts/verify.sh

##@ Security
## security: the eight blocking security gates  ·  what a PR must pass
##   secrets (gitleaks) · mal-diff (structural review of this branch's diff) ·
##   semgrep (SAST, ERROR blocks and WARNING reports) · deps (osv-scanner +
##   pip-audit, which also covers known-MALICIOUS packages via OSV's MAL- feed) ·
##   web (CDN pins, SRI, CSP, retire.js, install flags) · ct (secret-dependent branches) ·
##   esp (component registry pins) · attest (release provenance).
##   ~30s. Identical to what CI runs (ci.yml, via make verify), because both
##   call scripts/security.sh. Name one to run it alone:
##     make security GATES="semgrep deps"
##   ct reports "not checked" on macOS: there is no valgrind for darwin/arm64,
##   so it exits 2 rather than passing. CT_DOCKER=1 runs it in a container.
##   Slower analyses (full-history secrets, semgrep SARIF, Scorecard) are not
##   here: they run weekly in security-deep.yml and block nothing. CodeQL runs
##   under GitHub's default setup, configured in the repository settings.
security:
	@$(REPO_ROOT)/scripts/security.sh $(GATES)

## security-web: browser supply chain  ·  CDN pins, SRI, CSP, retire.js, install flags
##   Covers web-flasher/, web-twin/, release/*/FLASH.html and the docs theme.
##   Known debt lives in security/web-baseline.txt; an entry there that stops
##   matching FAILS the gate, so a line cannot outlive its problem.
security-web:
	@$(REPO_ROOT)/scripts/security-web.sh

## security-ct: constant-time  ·  secret-dependent branches in the CCC ladder
##   ctgrind under valgrind: the URSK is poisoned, so a branch on undefined data
##   IS a branch on the key. No valgrind on darwin/arm64 — the gate says so and
##   exits 2 rather than passing. CT_DOCKER=1 runs it in a container.
security-ct:
	@$(REPO_ROOT)/scripts/security-ct.sh

## security-workspace: the fetched dependencies  ·  west pins, ESP components, SBOM
##   `esp` needs nothing; `pins sbom vulns` need `make bootstrap` first and run
##   in the deep CI lane. WS_UPDATE_PINS=1 records the resolved pin set.
security-workspace:
	@$(REPO_ROOT)/scripts/security-workspace.sh $(GATES)

## security-fw: the shipped artifact  ·  key material, build-host paths, size
##   Runs on the nRF5340 DK image, so it needs `make nrf-build` first. That board
##   and not the CDK on purpose: security/fw-size-baseline.txt is keyed on the
##   artifact name and its numbers were measured there. FW_IMAGE=<path> points it
##   somewhere else, which also means comparing against a record it does not fit.
security-fw:
	@$(REPO_ROOT)/scripts/security-fw.sh

## security-attest: release provenance is configured  ·  and `verify <tag>` proves it works
security-attest:
	@$(REPO_ROOT)/scripts/security-attest.sh workflow
