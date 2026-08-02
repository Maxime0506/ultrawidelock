# mk/cdk.mk — the DWM3001CDK, this repo's primary target.
#
# Bare `build`, `flash`, `flash-erase` and `monitor` all mean this board and,
# unless you say otherwise, its Matter-over-Thread image. The standalone reader
# and the UWB self-test are the special cases and each keeps its own build
# directory, so no target can flash one image and then decode RTT against
# another's ELF.
#
# The app is firmware/ at the top of the tree, not a subdirectory of ports/:
# it is the product, and the other two boards are ports of it.

# Sysbuild names each image after the application directory, so the per-image
# artifacts live under <build>/firmware. Named once here because three targets
# reach into it and a wrong guess fails as "no ELF", which reads like a dead board.
CDK_IMAGE := firmware
CDK_APP   := $(REPO_ROOT)/firmware
CDK_BOARD := decawave_dwm3001cdk
CDK_CHIP  := nRF52833_xxAA

CDK_BUILD          ?= $(ALIRO_BUILD_ROOT)/cdk-matter
CDK_READER_BUILD   ?= $(ALIRO_BUILD_ROOT)/cdk-reader
CDK_SELFTEST_BUILD ?= $(ALIRO_BUILD_ROOT)/cdk-selftest
# Split out only so `monitor` can be pointed at an ELF without moving what the
# flash targets write. Same directory by default, which is the whole point.
CDK_RTT_BUILD      ?= $(CDK_BUILD)

# PRISTINE=1 forces a from-scratch build. `-p auto` re-runs CMake when the board
# or the app directory changes, and NOT when the -D flags do, so switching an
# existing build dir between the reader and the Matter build needs this.
CDK_PRISTINE := $(if $(PRISTINE),always,auto)

# ALIRO_TOOLCHAIN=env skips the nrfutil wrapper and runs west straight off PATH.
# scripts/build-nrf5340dk.sh carries the same escape hatch for the same reason:
# inside the NCS toolchain container CI uses, nrfutil's toolchain index is not
# reachable, so the wrapper cannot resolve a toolchain that is already there.
# firmware-builds.yml's dwm3001cdk job depends on this.
ifeq ($(ALIRO_TOOLCHAIN),env)
CDK_WEST := west
else
CDK_WEST := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VER) -- west
endif

# Every recipe runs from ./workspace so west finds its manifest.
CDK_RUN = cd $(REPO_ROOT)/workspace && $(CDK_WEST)

.PHONY: build rebuild reader selftest flash flash-erase monitor \
        cdk-aliro-matter-thread cdk-reader cdk-flash cdk-flash-erase cdk-rtt

##@ DWM3001CDK  ·  the lock (bare targets mean this board)
## build: the DWM3001CDK lock, reader + Matter over Thread  -> build/cdk-matter
##   One nRF52833: the Aliro reader, the DW3110's ranging, a hand-written Matter
##   node and an OpenThread MTD, in a part Nordic's own CHIP-based lock does not
##   fit in. Apple Home commissions it over BLE and it then shows a live lock
##   tile (firmware/README.md). Self-provisions, so it needs no USB console.
##   What flash, flash-erase and monitor all mean unless you say otherwise.
##   Options: PRISTINE=1  CDK_BUILD=<dir>  NCS_VER=<tag>
build:
	@$(CDK_RUN) build -p $(CDK_PRISTINE) -b $(CDK_BOARD) \
	  -d $(CDK_BUILD) $(CDK_APP) \
	  -- -DEXTRA_CONF_FILE=overlay-thread.conf -DCONFIG_ALIRO_MATTER_BLE=y
	@python3 $(REPO_ROOT)/scripts/spake2p_verifier.py \
	  --from-config $(CDK_BUILD)/$(CDK_IMAGE)/zephyr/.config

## rebuild: force a clean pristine build of the Matter image
rebuild:
	@$(MAKE) --no-print-directory build PRISTINE=1

## reader: the same board WITHOUT Matter        -> build/cdk-reader
##   Aliro reader and UWB only. Needs no Thread network and no commissioner,
##   which makes it the quickest way to a working board; the identity is typed
##   in over USB in provisioning mode (firmware/README.md).
##   Builds elsewhere on purpose, so flash and monitor keep meaning the Matter
##   image: pass CDK_BUILD=$(CDK_READER_BUILD) to those to work on this one.
##   Options: PRISTINE=1  CDK_READER_BUILD=<dir>
reader:
	@$(CDK_RUN) build -p $(CDK_PRISTINE) -b $(CDK_BOARD) \
	  -d $(CDK_READER_BUILD) $(CDK_APP)

## selftest: one-shot UWB init self-test at boot  -> build/cdk-selftest
##   The stage-3 bring-up check: uwb_selftest.c runs the full Aliro UWB start
##   path against a dummy URSK and logs the raw DEV_ID read over SPI (expect
##   0xDECA03xx). A wrong pin, a wrong SPI mode or an unpowered DW3110 all show
##   up as 0x00000000 or 0xFFFFFFFF. Reader config plus the overlay, no Matter.
##   Its RX diagnostics print continuously once the responder is listening, so
##   do not use this image when what you need to watch is bring-up.
##   Read it with: make monitor CDK_RTT_BUILD=$(CDK_SELFTEST_BUILD)
selftest:
	@$(CDK_RUN) build -p $(CDK_PRISTINE) -b $(CDK_BOARD) \
	  -d $(CDK_SELFTEST_BUILD) $(CDK_APP) \
	  -- -DEXTRA_CONF_FILE=overlays/uwb-selftest.conf

## flash: flash the DWM3001CDK over its on-board J-Link OB
##   Options: CDK_BUILD=<dir> (default build/cdk-matter)
flash:
	@$(CDK_RUN) flash -d $(CDK_BUILD)

## flash-erase: full chip erase + flash the DWM3001CDK
##   Costs everything the board learned at runtime: the Matter fabrics, the
##   reader identity and its trust anchors, so Apple Home has to commission it
##   again. It also destroys OpenThread's SRP client key. That used to be the
##   expensive part -- the next boot asked for the same name under a new key and
##   the border router refused it for up to 14 days -- and f7d3160 ended it by
##   giving the host name a suffix that dies with the key, so an erased board
##   now registers a name nobody owns.
##   To clear only what a controller can see, hold SW2 through reset instead
##   (ALIRO_FACTORY_RESET_BUTTON): same effect on fabrics and anchors, and it
##   keeps the Thread settings, so the board comes back on the name it had.
##   Options: CDK_BUILD=<dir> (default build/cdk-matter)
flash-erase:
	@$(CDK_RUN) flash --erase -d $(CDK_BUILD)

## monitor: stream the DWM3001CDK's console over RTT  ·  Ctrl-C to stop
##   This board has no UART console (CONFIG_UART_CONSOLE=n), so `make nrf-term`
##   does not reach it and RTT is the only log there is. probe-rs re-reads
##   _SEGGER_RTT out of the ELF on every attach, so a rebuild that moves the
##   control block needs no change here -- but the ELF must be the one you
##   FLASHED. Attach with an ELF you only built and probe-rs reads a stale
##   address and prints nothing, which looks exactly like a dead board.
##   READ THE FIRST BLOCK WITH SUSPICION. The RTT ring lives in its own section
##   at the bottom of RAM and is NOT cleared by a reset, so everything printed
##   before the "*** Booting nRF Connect SDK ***" line is the PREVIOUS run --
##   old firmware, old bug. That has twice been mistaken for the current one.
##   The J-Link tools are not an alternative: JLinkRTTLogger cannot find this
##   control block, with -RTTAddress or with an all-of-RAM -RTTSearchRanges.
##   Options: CDK_RTT_BUILD=<dir> (defaults to CDK_BUILD, so it follows the
##            image the flash targets wrote)
monitor:
	@command -v probe-rs >/dev/null 2>&1 || { printf '  probe-rs not found  ·  install: make tools-install\n' >&2; exit 1; }
	@test -f $(CDK_RTT_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf || { printf '  no ELF at %s/$(CDK_IMAGE)/zephyr/zephyr.elf  ·  build it first\n' '$(CDK_RTT_BUILD)' >&2; exit 1; }
	@# The code you would be asked for while watching this. Never fatal: the
	@# reader build has no Matter symbols and a console is still worth having.
	@python3 $(REPO_ROOT)/scripts/spake2p_verifier.py \
	  --from-config $(CDK_RTT_BUILD)/$(CDK_IMAGE)/zephyr/.config 2>/dev/null || true
	@probe-rs attach --chip $(CDK_CHIP) $(CDK_RTT_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf

# ---- compatibility aliases ---------------------------------------------------
# The names these targets had while the CDK still lived under ports/. Each does
# the work and prints where it moved to, so a bookmarked command keeps working
# and says so once. Not in the help list: the new names are above.
cdk-aliro-matter-thread:
	@printf '  cdk-aliro-matter-thread is now `make build`\n'
	@$(MAKE) --no-print-directory build

cdk-reader:
	@printf '  cdk-reader is now `make reader`\n'
	@$(MAKE) --no-print-directory reader

cdk-flash:
	@printf '  cdk-flash is now `make flash`\n'
	@$(MAKE) --no-print-directory flash

cdk-flash-erase:
	@printf '  cdk-flash-erase is now `make flash-erase`\n'
	@$(MAKE) --no-print-directory flash-erase

cdk-rtt:
	@printf '  cdk-rtt is now `make monitor`\n'
	@$(MAKE) --no-print-directory monitor
