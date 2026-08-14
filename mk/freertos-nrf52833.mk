# mk/freertos-nrf52833.mk - standalone FreeRTOS port for the DWM3001CDK.
# Target firmware is being built from the Qorvo board/FreeRTOS base plus a
# maintained upstream OpenThread and Nordic radio integration. The host port
# test compiles the production OSAL and OpenThread runtime implementations.

.PHONY: check-freertos freertos-port-test freertos-platform-check freertos-radio-source-check freertos-ble-source-check freertos-crypto-source-check freertos-ncs-source-check freertos-build freertos-sign freertos-keycheck freertos-flash freertos-ota-patch freertos-ota-push freertos-dfu

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

# The cross toolchain is looked up on PATH unless ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR names a
# bin directory, so a toolchain installed outside the system prefix can be used
# without putting it on PATH for every other build.
#
# Forwarded from the environment when set, rather than left to CMake's cache.
# The cache only remembers it until someone deletes the build directory, and the
# failure that follows -- "nosys.specs is missing" -- reads like a broken
# toolchain rather than a lost setting.
FREERTOS_TOOLCHAIN_ARG = $(if $(ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR),-DULTRAWIDELOCK_ARM_TOOLCHAIN_DIR=$(ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR))

# Off by default until the DW3110 arm deadline is measured under it. Passed
# explicitly on every configure rather than left to the cache, for the same
# reason as the toolchain above: a setting the cache remembers and the command
# line does not is a setting nobody can see in the build they just ran.
FREERTOS_LTO ?= OFF

# The USB provisioning console: 18,151 B of flash and 10,249 B of RAM, all of
# it serving a mode that runs only when SW2 is held through reset. A Matter
# node self-provisions and does not need it.
FREERTOS_PROV_CONSOLE ?= ON

# The Matter node. Off until the BLE commissioning transport exists: the
# protocol and the Thread half link today, but a node a commissioner cannot
# reach over BLE is not one anybody can add to a home.
FREERTOS_MATTER ?= OFF

# 1 error, 2 warning, 3 info, 4 debug. The gate is at the call site, so a lower
# level removes the format strings too rather than only their output.
FREERTOS_LOG_LEVEL ?= 3

# RTT up-buffer bytes. Empty keeps the port's 1 kB, which reaches the end of
# bring-up and no further because a full buffer drops writes instead of
# overwriting. Raise it to watch anything talkative -- a BTP handshake, a
# commissioning attempt -- and remember it is static RAM on a part with about
# 8 kB spare.
FREERTOS_RTT_BUFFER ?=

# GCC -flto-partition. 1to1 keeps code generation on translation-unit
# boundaries; it was forced by an assembler "offset out of range" when the
# kernel, device and Thread layers were still inside the LTO set, and it costs
# most of the cross-unit code motion LTO exists for.
FREERTOS_LTO_PARTITION ?= 1to1

# Turning the console off removes the USBD vector, and freertos-vector-check.sh
# is told which OWNER went rather than being left to infer it from a missing
# handler. Inferring it would make a deliberate exclusion and a dropped handler
# look identical, which is the failure that check exists to prevent.
FREERTOS_VECTOR_EXCLUDES = $(if $(filter OFF,$(FREERTOS_PROV_CONSOLE)),--without=usb_provisioning_console)

FREERTOS_CMAKE_ARGS = \
	$(FREERTOS_TOOLCHAIN_ARG) \
	-DULTRAWIDELOCK_LTO=$(FREERTOS_LTO) \
	-DULTRAWIDELOCK_PROV_CONSOLE=$(FREERTOS_PROV_CONSOLE) \
	-DULTRAWIDELOCK_MATTER=$(FREERTOS_MATTER) \
	-DULTRAWIDELOCK_LOG_LEVEL=$(FREERTOS_LOG_LEVEL) \
	-DULTRAWIDELOCK_LOG_RTT_BUFFER=$(FREERTOS_RTT_BUFFER) \
	-DULTRAWIDELOCK_LTO_PARTITION=$(FREERTOS_LTO_PARTITION) \
	-DULTRAWIDELOCK_DFU_KEY=$(SIGN_KEY) \
	-DCMAKE_TOOLCHAIN_FILE=$(REPO_ROOT)/ports/freertos-nrf52833/cmake/arm-none-eabi.cmake \
	-DULTRAWIDELOCK_QORVO_SDK_DIR=$(QORVO_SDK_DIR) \
	-DULTRAWIDELOCK_NCS_WORKSPACE=$(NCS_WORKSPACE) \
	-DULTRAWIDELOCK_NIMBLE_DIR=$(NIMBLE_SOURCE) \
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
	@cmake --build $(FREERTOS_BUILD_DIR) --target ultrawidelock_uwb_reach
	@# Every vector peripherals.yml gives an owner must reach that owner. The
	@# vector table is data, so a weak alias no owner overrode links cleanly and
	@# resolves to default_handler -- an infinite loop the first interrupt falls
	@# into. RNG and RTC2 both shipped that way and cost a hardware debugging
	@# session; this makes the next one a build failure.
	@ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR=$(ULTRAWIDELOCK_ARM_TOOLCHAIN_DIR) \
		$(REPO_ROOT)/scripts/freertos-vector-check.sh \
		$(FREERTOS_BUILD_DIR)/dwm3001cdk-lock-freertos.elf \
		$(FREERTOS_VECTOR_EXCLUDES)
	@# The shared Matter Thread transport is compiled from the Zephyr port
	@# tree unmodified. A rename on that side compiles fine THERE and lands
	@# here as an unknown settings path at run time, whose only symptom is an
	@# SRP host name that changes every boot -- a node that attaches to
	@# Thread and never registers. Held closed at build time instead.
	@$(if $(filter ON,$(FREERTOS_MATTER)),$(REPO_ROOT)/scripts/freertos-matter-source-check.sh)
	@# The pairing code, re-derived rather than quoted. The device stores only
	@# the SPAKE2+ verifier and never the passcode, so nothing on the board can
	@# notice that the two stopped agreeing -- and they did, silently, when the
	@# verifier was carried into this port's header and a comment naming CHIP's
	@# test passcode came with it. On hardware that reads as a healthy board
	@# that reaches Pake2 and gets hung up on. The Zephyr build has run this
	@# check from its .config all along; this is the same check for a build that
	@# has no .config, and it fails the build rather than printing a code that
	@# would not work.
	@$(if $(filter ON,$(FREERTOS_MATTER)),python3 $(REPO_ROOT)/scripts/freertos-pairing-code.py)
	@# newlib-nano's printf does not implement ll. A format it cannot honour
	@# consumes the wrong argument width, so the next %s dereferences a data
	@# value -- a bus fault into default_handler, which spins and takes the
	@# tick with it. The board then prints a complete boot log and goes
	@# silent, which is why this is a build failure and not a review item.
	@$(REPO_ROOT)/scripts/freertos-printf-check.sh
	@# And sign it, when a key was named. Part of the build rather than a
	@# target someone remembers to run: the linker script puts this image at
	@# 0xa200, the MCUboot application slot, so an UNSIGNED image is not a
	@# thing the board can boot -- it is a file that looks like firmware and
	@# is refused by the bootloader with nothing on the wire to say why.
	@$(if $(FREERTOS_SIGN_KEY),$(MAKE) --no-print-directory freertos-sign)

# ---- signing and over-the-air update ----------------------------------------
#
# The Zephyr build signs inside sysbuild; this port has no sysbuild, so the
# same imgtool invocation lives here. The parameters are NOT free choices --
# they describe the partition map the installed bootloader was built against,
# and a disagreement produces an image that flashes cleanly and never boots:
#
#   --slot-size 0x6a000   mcuboot_primary, from the Zephyr build's partitions.yml
#   --header-size 0x200   mcuboot_pad; also the ld script's 0xa200 minus 0xa000
#   --align 4             nRF52833 flash write unit
#
# Taken from the command the Zephyr build actually runs, not from imgtool's
# defaults, so the two ports produce images the same bootloader accepts.
FREERTOS_SLOT_SIZE   ?= 0x6a000
FREERTOS_HEADER_SIZE ?= 0x200
FREERTOS_IMG_VERSION ?= 0.0.0+0
FREERTOS_PATCH_STAGING_SIZE ?= 0xa000

FREERTOS_HEX        := $(FREERTOS_BUILD_DIR)/dwm3001cdk-lock-freertos.hex
# THE .hex, NOT THE .bin, for the reason mk/cdk.mk sets out at CDK_SIGNED_HEX:
# ECDSA signatures are randomised, so signing twice gives two artifacts holding
# the same code under different signatures. A delta is computed against the
# bytes on the part, so only the artifact that was flashed can be the from-image.
FREERTOS_SIGNED_HEX := $(FREERTOS_BUILD_DIR)/dwm3001cdk-lock-freertos.signed.hex
FREERTOS_DEPLOYED   ?= $(ULTRAWIDELOCK_BUILD_ROOT)/freertos-deployed/dwm3001cdk-lock-freertos.signed.hex
FREERTOS_PATCH      ?= $(FREERTOS_BUILD_DIR)/update.wdfu

# The key that signs the image. Defaults to SIGN_KEY, the checkout-wide default
# the Zephyr build already uses, so one board trusts one key across both ports.
FREERTOS_SIGN_KEY ?= $(SIGN_KEY)

FREERTOS_IMGTOOL ?= $(NCS_WORKSPACE)/bootloader/mcuboot/scripts/imgtool.py

## freertos-sign: sign the built image for the MCUboot slot the board runs
freertos-sign: $(CDK_OTA_PY)
	@test -n '$(FREERTOS_SIGN_KEY)' || { \
	  printf '  set SIGN_KEY=<absolute path to the signing key>\n' >&2; \
	  printf '  NEVER run `make dfu-key` to make one: it mints a NEW keypair, and\n' >&2; \
	  printf '  every already-flashed board refuses images signed by anything but\n' >&2; \
	  printf '  the key its bootloader was built with.\n' >&2; exit 2; }
	@test -f '$(FREERTOS_HEX)' || { \
	  printf '  no image at %s  ·  run `make freertos-build` first\n' '$(FREERTOS_HEX)' >&2; \
	  exit 2; }
	@test -f '$(FREERTOS_IMGTOOL)' || { \
	  printf '  no imgtool at %s  ·  set NCS_WORKSPACE\n' '$(FREERTOS_IMGTOOL)' >&2; exit 2; }
	@# imgtool needs click and intelhex on top of what the OTA venv carries for
	@# the patch tooling. Installed here rather than in the venv recipe: that
	@# venv belongs to the update path, and a signing dependency added to it
	@# silently would make `make ota-patch` fail for a reason about signing.
	@'$(CDK_OTA_VENV)/bin/pip' install --quiet --disable-pip-version-check \
	  click intelhex pyyaml cbor
	@$(CDK_OTA_PY) '$(FREERTOS_IMGTOOL)' sign \
	  --version $(FREERTOS_IMG_VERSION) --align 4 \
	  --slot-size $(FREERTOS_SLOT_SIZE) --pad-header \
	  --header-size $(FREERTOS_HEADER_SIZE) \
	  -k '$(FREERTOS_SIGN_KEY)' '$(FREERTOS_HEX)' '$(FREERTOS_SIGNED_HEX)'
	@printf '  signed  ·  %s\n' '$(FREERTOS_SIGNED_HEX)'

## freertos-keycheck: will the board's installed bootloader accept this image?
#
# Reads the bootloader off the part rather than trusting a build directory, and
# compares its compiled-in public key against the image's KEYHASH TLV. Everything
# it touches is public; it never reads a private key. This is the check that was
# missing when three candidate images were signed and flashed one after another,
# each refused, with nothing to say which key the board actually trusted.
freertos-keycheck:
	@test -f '$(FREERTOS_SIGNED_HEX)' || { \
	  printf '  no signed image  ·  run `make freertos-sign`\n' >&2; exit 2; }
	@mkdir -p '$(FREERTOS_BUILD_DIR)'
	@printf 'si SWD\nspeed 4000\ndevice NRF52833_XXAA\nconnect\nsavebin %s 0x0 0xa000\nq\n' \
	  '$(FREERTOS_BUILD_DIR)/installed_boot.bin' > '$(FREERTOS_BUILD_DIR)/keycheck.jlink'
	@JLinkExe -nogui 1 -CommandFile '$(FREERTOS_BUILD_DIR)/keycheck.jlink' >/dev/null 2>&1 || true
	@test -s '$(FREERTOS_BUILD_DIR)/installed_boot.bin' || { \
	  printf '  could not read the bootloader over SWD  ·  is the probe attached?\n' >&2; exit 2; }
	@python3 $(REPO_ROOT)/scripts/mcuboot-keyhash-check.py \
	  --bootloader '$(FREERTOS_BUILD_DIR)/installed_boot.bin' \
	  --image '$(FREERTOS_SIGNED_HEX)'

## freertos-flash: write the signed image to the app slot, leaving the KV store
#
# THE APP SLOT ONLY. The settings/KV store at 0x7e000 holds the fabric and the
# credential reader identity, and a chip erase takes both -- which costs a re-pair
# and a re-provision, not a reflash. `loadfile` writes only the sectors the hex
# names, and the signed hex names 0xa000..0x74000.
freertos-flash: freertos-keycheck
	@printf 'si SWD\nspeed 4000\ndevice NRF52833_XXAA\nconnect\nloadfile %s\nr\ngo\nq\n' \
	  '$(FREERTOS_SIGNED_HEX)' > '$(FREERTOS_BUILD_DIR)/flash.jlink'
	@JLinkExe -nogui 1 -CommandFile '$(FREERTOS_BUILD_DIR)/flash.jlink'
	@# What the board is running, so a delta has a from-image. The board
	@# cannot be asked over the air, so if this record goes stale the update
	@# is REFUSED rather than mis-applied -- the patch header carries a CRC of
	@# the from-image and the bootloader checks it.
	@mkdir -p '$(dir $(FREERTOS_DEPLOYED))'
	@cp '$(FREERTOS_SIGNED_HEX)' '$(FREERTOS_DEPLOYED)'
	@printf '  recorded as deployed  ·  %s\n' '$(FREERTOS_DEPLOYED)'

## freertos-ota-patch: build a signed delta from the deployed image to the built one
#
# --memory-size and --staging-size rather than --build-dir: that flag reads a
# partitions.yml, which sysbuild writes and this port has none of. The two sizes
# are the same partitions by another route, and the tool offers this pair for
# exactly this case.
freertos-ota-patch: $(CDK_OTA_PY)
	@if [ ! -f '$(FREERTOS_DEPLOYED)' ]; then \
	  printf '  no record of what the board is running  ·  %s\n' '$(FREERTOS_DEPLOYED)' >&2; \
	  printf '  A delta needs the image it starts from. Either `make freertos-flash`\n' >&2; \
	  printf '  once over SWD, or point FREERTOS_DEPLOYED at the signed .hex it runs.\n' >&2; \
	  exit 1; \
	fi
	@test -f '$(FREERTOS_SIGNED_HEX)' || { \
	  printf '  no signed image  ·  run `make freertos-sign`\n' >&2; exit 2; }
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/ultrawidelock_patch.py build \
	  --from '$(FREERTOS_DEPLOYED)' --to '$(FREERTOS_SIGNED_HEX)' \
	  --memory-size $(FREERTOS_SLOT_SIZE) --staging-size $(FREERTOS_PATCH_STAGING_SIZE) \
	  --key '$(FREERTOS_SIGN_KEY)' --out '$(FREERTOS_PATCH)'

## freertos-ota-push: send an already-built patch over Bluetooth
#
# The board refuses every frame until an update window is open: press SW2, or
# open a Matter commissioning window, which opens this one alongside it.
freertos-ota-push: $(CDK_OTA_PY)
	@test -f '$(FREERTOS_PATCH)' || { \
	  printf '  no patch at %s  ·  run `make freertos-ota-patch`\n' '$(FREERTOS_PATCH)' >&2; \
	  exit 1; }
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/ultrawidelock_push.py '$(FREERTOS_PATCH)' \
	  $(if $(OTA_NAME),--name '$(OTA_NAME)')
	@mkdir -p '$(dir $(FREERTOS_DEPLOYED))' && cp '$(FREERTOS_SIGNED_HEX)' '$(FREERTOS_DEPLOYED)'
	@printf '  recorded as deployed  ·  %s\n' '$(FREERTOS_DEPLOYED)'

## freertos-dfu: update the board over Bluetooth  ·  no cable, no probe
#
# Deliberately does NOT rebuild. `make dfu` on the Zephyr side does, and can,
# because its build is one command; this port's build needs the four
# configuration switches passed on the command line, and a rebuild here with
# different switches would produce a delta against an image nobody asked for.
freertos-dfu:
	@$(MAKE) --no-print-directory freertos-ota-patch
	@$(MAKE) --no-print-directory freertos-ota-push
