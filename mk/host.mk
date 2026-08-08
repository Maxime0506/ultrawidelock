# mk/host.mk — everything that runs on this machine: the host suites. No NCS
# toolchain, no ESP-IDF, no hardware. Output lands under build/host.

.PHONY: test test-san coverage cbmc check drift seam

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

## cbmc: bounded model-check the wire parsers  ·  memory-safety proof
##   Proves no out-of-bounds / bad-pointer access for all inputs up to each
##   harness bound. Needs cbmc on PATH (brew install cbmc / apt install cbmc).
cbmc:
	@$(REPO_ROOT)/tests/host/cbmc.sh

## check: every host-side suite under one banner  ->  live rows + summary table
##   Parallel by default; SERIAL=1 streams suites one at a time, SUITES="..."
##   scopes (firmware shared). The pre-push look, runnable any time.
check:
	@$(REPO_ROOT)/scripts/test-runner.sh

## drift: one constant, one number  ·  Kconfig and C must agree
##   A value that Kconfig and a C fallback both name is written down twice, and
##   the disagreement is silent: the host suites compile the C fallback while the
##   firmware compiles the Kconfig value, so both sides pass while testing
##   different software. This re-derives both from structure — `config X` /
##   `default V` against `#ifndef CONFIG_X` / `#define CONFIG_X V` — and fails
##   when they diverge. No file:line, no quoted prose: move a definition or
##   reflow a file and it still reads the same values.
drift:
	@python3 $(REPO_ROOT)/tests/tooling/drift_check.py

## seam: no call reaches the radio past the CCC STS seam
##   Four decadriver entry points carry engine behaviour a caller must not skip.
##   Bypassing one is SILENT on the bench — the radio arms, ranging runs, and the
##   phone simply never unlocks, because the STS never matched. A link-time
##   interposer used to make that structurally impossible; this buys the
##   guarantee back by scanning. --self-test proves the detector can still fail.
seam:
	@$(REPO_ROOT)/tests/tooling/uwb_seam_check.sh


