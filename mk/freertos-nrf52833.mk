# mk/freertos-nrf52833.mk - standalone FreeRTOS port for the DWM3001CDK.
# Target firmware is being built from the Qorvo board/FreeRTOS base plus a
# maintained upstream OpenThread and Nordic radio integration. The host port
# test compiles the production OSAL and OpenThread runtime implementations.

.PHONY: check-freertos freertos-port-test freertos-platform-check freertos-radio-source-check freertos-ble-source-check freertos-crypto-source-check freertos-ncs-source-check freertos-build

##@ DWM3001CDK FreeRTOS  ·  custom radio port in progress
## check-freertos: every host suite plus the opt-in FreeRTOS port contract
#
# The FreeRTOS suite is deliberately outside `make check`. The port has no
# hardware verdict yet -- no bring-up, no BLE/Thread coexistence proof, none of
# the four release gates -- so an unfinished port cannot turn this repository
# red. This target is how someone working on it runs everything at once.
check-freertos:
	@SUITES="firmware shared sdk drift seam scope purity freertos" \
		$(REPO_ROOT)/scripts/test-runner.sh

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

## freertos-crypto-source-check: verify the pinned Mbed TLS tree against the selected crypto backend
freertos-crypto-source-check:
	@if [ -z "$(NCS_WORKSPACE)" ]; then \
		printf '  Set NCS_WORKSPACE=<path-to-ncs-workspace>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-crypto-source-check.sh "$(NCS_WORKSPACE)"

## freertos-ncs-source-check: build and run the pinned NCS sources (HCI dispatcher, high-precision timer)
freertos-ncs-source-check:
	@if [ -z "$(NCS_WORKSPACE)" ]; then \
		printf '  Set NCS_WORKSPACE=<path-to-ncs-workspace>.\n' >&2; \
		exit 2; \
	fi
	@$(REPO_ROOT)/scripts/freertos-ncs-source-check.sh "$(NCS_WORKSPACE)"

# Where the target build looks for the three vendor trees. Each may be
# overridden on the command line; the defaults assume the west workspace and the
# extracted Qorvo SDK sit side by side, which is what bootstrap.sh produces.
NCS_WORKSPACE ?= $(REPO_ROOT)/workspace
QORVO_SDK_DIR ?= $(NCS_WORKSPACE)/qorvo-dw3-qm33-sdk-1.1.1
NIMBLE_SOURCE ?= $(NCS_WORKSPACE)/mynewt-nimble
FREERTOS_BUILD_DIR ?= $(REPO_ROOT)/build/freertos-nrf52833

# The cross toolchain is looked up on PATH unless WOZ_ARM_TOOLCHAIN_DIR names a
# bin directory, so a toolchain installed outside the system prefix can be used
# without putting it on PATH for every other build.
FREERTOS_CMAKE_ARGS = \
	-DCMAKE_TOOLCHAIN_FILE=$(REPO_ROOT)/ports/freertos-nrf52833/cmake/arm-none-eabi.cmake \
	-DWOZ_QORVO_SDK_DIR=$(QORVO_SDK_DIR) \
	-DWOZ_NCS_WORKSPACE=$(NCS_WORKSPACE) \
	-DWOZ_NIMBLE_DIR=$(NIMBLE_SOURCE) \
	-DCMAKE_BUILD_TYPE=MinSizeRel

## freertos-build: build the nRF52833 target image and report its flash and RAM cost
freertos-build:
	@if [ ! -d "$(QORVO_SDK_DIR)" ]; then \
		printf '  Extract the pinned Qorvo SDK to %s first, or set QORVO_SDK_DIR.\n' \
			'$(QORVO_SDK_DIR)' >&2; \
		printf '  Verify the archive with make freertos-platform-check QORVO_SDK_ARCHIVE=<zip>.\n' >&2; \
		exit 2; \
	fi
	@if [ ! -d "$(NCS_WORKSPACE)/modules/hal/nordic/nrfx" ]; then \
		printf '  Set NCS_WORKSPACE=<path-to-ncs-workspace>; %s has no nrfx.\n' \
			'$(NCS_WORKSPACE)' >&2; \
		exit 2; \
	fi
	@cmake -S $(REPO_ROOT)/apps/dwm3001cdk-lock-freertos -B $(FREERTOS_BUILD_DIR) \
		-G Ninja $(FREERTOS_CMAKE_ARGS)
	@cmake --build $(FREERTOS_BUILD_DIR)
	@# What the UWB layer will cost once something calls it. The image itself
	@# cannot say: nothing reaches the layer yet, so it is collected away and
	@# the flash figure above is silent about it. Printed on every build rather
	@# than kept as a target someone remembers to run, because this port has
	@# already had a check rot in exactly that position.
	@cmake --build $(FREERTOS_BUILD_DIR) --target woz_uwb_reach
