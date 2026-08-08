# mk/host.mk — everything that runs on this machine: the host suites. No NCS
# toolchain, no ESP-IDF, no hardware. Output lands under build/host.

.PHONY: test test-san coverage cbmc check drift seam purity

##@ Test
## test: run the host test suite for our logic  (no NCS toolchain / hardware)
test:
	@$(REPO_ROOT)/tests/host/run.sh

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

## drift: one constant, one number  ·  Kconfig and C must agree
drift:
	@python3 $(REPO_ROOT)/tests/tooling/drift_check.py

## seam: no call reaches the radio past the CCC STS seam
seam:
	@$(REPO_ROOT)/tests/tooling/uwb_seam_check.sh

## purity: no platform includes or kernel calls in modules/ outside woz_port
purity:
	@$(REPO_ROOT)/tests/tooling/port_purity_check.sh


