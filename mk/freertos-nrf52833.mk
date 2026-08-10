# mk/freertos-nrf52833.mk - standalone FreeRTOS port for the DWM3001CDK.
# Target firmware is being built from the Qorvo board/FreeRTOS base plus a
# maintained upstream OpenThread and Nordic radio integration. The host port
# test compiles the production OSAL and OpenThread runtime implementations.

.PHONY: freertos-port-test freertos-platform-check freertos-radio-source-check freertos-ble-source-check freertos-hci-dispatcher-check freertos-build

##@ DWM3001CDK FreeRTOS  ·  custom radio port in progress
## freertos-port-test: compile and run the standalone FreeRTOS port contract on the host
freertos-port-test:
	@$(REPO_ROOT)/tests/ports/freertos-nrf52833/run.sh

## freertos-platform-check: verify the pinned Qorvo v1.1.1 source base and radio primitives
freertos-platform-check:
	@if [ -z "$(QORVO_SDK_ARCHIVE)" ]; then \
		printf '  Set QORVO_SDK_ARCHIVE=<path-to-DW3_QM33_SDK_1.1.1.zip>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-platform-check.sh "$(QORVO_SDK_ARCHIVE)"

## freertos-radio-source-check: verify the pinned OpenThread, MPSL/SDC, and nRF 802.15.4 source set
freertos-radio-source-check:
	@if [ -z "$(NCS_WORKSPACE)" ]; then \
		printf '  Set NCS_WORKSPACE=<path-to-ncs-workspace>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-radio-source-check.sh "$(NCS_WORKSPACE)"

## freertos-ble-source-check: verify the pinned NimBLE FreeRTOS, GATT, and L2CAP CoC sources
freertos-ble-source-check:
	@if [ -z "$(NIMBLE_SOURCE)" ]; then \
		printf '  Set NIMBLE_SOURCE=<path-to-mynewt-nimble-checkout>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-ble-source-check.sh "$(NIMBLE_SOURCE)"

## freertos-hci-dispatcher-check: build and run the pinned HCI opcode dispatcher Zephyr-free
freertos-hci-dispatcher-check:
	@if [ -z "$(NCS_WORKSPACE)" ]; then \
		printf '  Set NCS_WORKSPACE=<path-to-ncs-workspace>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-hci-dispatcher-check.sh "$(NCS_WORKSPACE)"

## freertos-build: gated until the custom target build graph is assembled
freertos-build:
	@printf '  FreeRTOS target build is not assembled yet: custom OpenThread/radio integration is in progress.\n' >&2
	@printf '  See internal/dwm3001cdk-freertos-platform-qualification.md for the selected architecture.\n' >&2
	@printf '  Run make freertos-port-test for the implemented RTOS and OpenThread runtime.\n' >&2
	@exit 2
