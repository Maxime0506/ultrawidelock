# mk/host.mk — everything that runs on this machine: the host suites. No NCS
# toolchain, no ESP-IDF, no hardware. Output lands under build/host.

.PHONY: test sdk-check test-san coverage cbmc check drift seam purity

##@ Test
## test: run the host test suite for our logic  (no NCS toolchain / hardware)
test:
	@$(REPO_ROOT)/tests/host/run.sh

## sdk-check: install the CMake package and build an external C consumer
sdk-check:
	@$(REPO_ROOT)/tests/sdk/run.sh

## test-san: host suite rebuilt under ASan + UBSan  ·  memory-bug gate
test-san:
	@SAN=1 $(REPO_ROOT)/tests/host/run.sh

## coverage: line coverage of every host suite, 0% rows for the rest  ->  table + HTML
coverage:
	@$(REPO_ROOT)/tests/host/coverage.sh

## cbmc: bounded model-check the wire parsers  ·  memory-safety proof
cbmc:
	@$(REPO_ROOT)/tests/host/cbmc.sh

## check: every host-side suite under one banner  ->  live rows + summary table
check:
	@$(REPO_ROOT)/scripts/test-runner.sh

## drift: constants and integration patch-state identity stay exact
drift:
	@$(REPO_ROOT)/tests/tooling/drift_suite.sh

## seam: no call reaches the radio past the CCC STS seam
seam:
	@$(REPO_ROOT)/tests/tooling/uwb_seam_check.sh

## purity: modules/ names no OS, each port tree names only its own
purity:
	@$(REPO_ROOT)/tests/tooling/port_purity_check.sh
