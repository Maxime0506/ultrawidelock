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

# RELEASE=1 appends the release overlay, which trades the 8 KB RTT ring for
# 7,168 B of RAM. Semicolon because EXTRA_CONF_FILE is a CMake list and later
# files win, so this can only ever override overlay-thread.conf. Note `-p auto`
# does NOT re-run CMake when these -D flags change (see CDK_PRISTINE above), so
# switching RELEASE on or off in an existing build dir needs PRISTINE=1.
#
# LTO IS ON BY DEFAULT. `LTO=0` (also n/no/off) opts out, which is what you want
# when a stack trace has to name every frame.
#
# It was gated behind a walk-up unlock on hardware, because a size number cannot
# vouch for the ~1836 us ranging arm deadline under whole-program codegen. That
# gate passed 2026-08-02: two grants, a relock at 336 cm, a re-grant on the
# return leg at 45 cm, 604 RX with the STS live throughout.
#
# Applied to BOTH variants on purpose. Debug and release then differ only in the
# RTT ring, their codegen is identical, and what you debug on the bench is what
# ships -- which matters most for exactly the timing bugs LTO could cause.
# RELEASE stays what it already claims to be: a RAM lever, not a codegen one.
# Worth 41,084 B of flash. See firmware/overlay-lto.conf.
LTO      ?= 1
CDK_LTO  := $(filter-out 0 n no off N NO OFF,$(LTO))
CDK_CONF := overlay-thread.conf$(if $(RELEASE),;overlay-release.conf)$(if $(CDK_LTO),;overlay-lto.conf)

# ---- image signing -----------------------------------------------------------
# Which private key signs the image is the whole answer to "what will this lock
# boot", so it is never left to MCUboot's default -- that default is a key
# published in MCUboot's own repository. firmware/sysbuild.cmake refuses to
# build with any of the seven, and firmware/keys/README.md has the rest.
#
# The path MUST be absolute. Sysbuild hands this symbol to the bootloader image
# through set_config_string(), never through a .conf file, so MCUboot's own
# base-directory search finds nothing and a relative path falls through to
# ${MCUBOOT_DIR}/<path> -- resolving INSIDE the MCUboot repo, which is how a
# wrong path turns silently back into the demo key.
#
# The inner quotes are part of the value: zephyr/cmake/modules/kconfig.cmake:264
# writes a command-line cache variable through verbatim, and a Kconfig string
# without quotes is a syntax error rather than a fallback.
#
# Applied to all three images. `-p auto` does NOT re-run CMake when a -D flag
# changes (see CDK_PRISTINE), so pointing an existing build dir at a new key
# needs PRISTINE=1.
CDK_KEY  ?= $(REPO_ROOT)/firmware/keys/mcuboot_ec_p256.pem
CDK_SIGN := -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE='"$(CDK_KEY)"'

# What `make dfu` uploads: the application plus MCUboot's header and P-256 TLVs.
# NOT zephyr.bin, which is unsigned and which MCUboot will refuse.
CDK_SIGNED := $(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.signed.bin
DFU_BAUD   ?= 115200

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

.PHONY: build rebuild reader selftest flash flash-erase monitor dfu dfu-key \
        cdk-aliro-matter-thread cdk-reader cdk-flash cdk-flash-erase cdk-rtt

##@ DWM3001CDK  ·  the lock (bare targets mean this board)
## build: the DWM3001CDK lock, reader + Matter over Thread  -> build/cdk-matter
##   One nRF52833: the Aliro reader, the DW3110's ranging, a hand-written Matter
##   node and an OpenThread MTD, in a part Nordic's own CHIP-based lock does not
##   fit in. Apple Home commissions it over BLE and it then shows a live lock
##   tile (firmware/README.md). Self-provisions, so it needs no USB console.
##   What flash, flash-erase and monitor all mean unless you say otherwise.
##   RELEASE=1 gives up the 8 KB RTT ring for 7,168 B of RAM. Debug is the
##   default on purpose: on a release image a fault reads as a hang, because a
##   1 KB ring truncates the boot log and NO_BLOCK_SKIP drops the NEWEST lines.
##   LTO is ON by default and worth 41,084 B; LTO=0 opts out when you need a
##   stack trace to name every frame.
##   Options: PRISTINE=1  RELEASE=1  LTO=0  CDK_BUILD=<dir>  NCS_VER=<tag>
build:
	@$(CDK_RUN) build -p $(CDK_PRISTINE) -b $(CDK_BOARD) \
	  -d $(CDK_BUILD) $(CDK_APP) \
	  -- -DEXTRA_CONF_FILE="$(CDK_CONF)" -DCONFIG_ALIRO_MATTER_BLE=y $(CDK_SIGN)
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
	  -d $(CDK_READER_BUILD) $(CDK_APP) \
	  -- $(CDK_SIGN)

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
	  -- -DEXTRA_CONF_FILE=overlays/uwb-selftest.conf $(CDK_SIGN)

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

## dfu-key: generate this checkout's MCUboot signing key  ·  once per clone
##   ECDSA P-256 into firmware/keys/, gitignored. Every image build needs it:
##   without a key firmware/sysbuild.cmake fails the build rather than let it
##   fall back to MCUboot's PUBLISHED demo key.
##   REFUSES TO OVERWRITE an existing key. Replacing it strands every board
##   already carrying the old public half, and with one slot there is no
##   previous image to fall back to. firmware/keys/README.md has the rotation.
##   Options: CDK_KEY=<path>
dfu-key:
	@if [ -f '$(CDK_KEY)' ]; then \
	  printf '  key exists, keeping it  ·  %s\n' '$(CDK_KEY)'; exit 0; \
	fi; \
	mkdir -p '$(dir $(CDK_KEY))'; \
	if command -v openssl >/dev/null 2>&1; then \
	  openssl ecparam -name prime256v1 -genkey -noout -out '$(CDK_KEY)'; \
	else \
	  python3 -c 'import sys;from cryptography.hazmat.primitives.asymmetric import ec;from cryptography.hazmat.primitives import serialization as s;open(sys.argv[1],"wb").write(ec.generate_private_key(ec.SECP256R1()).private_bytes(s.Encoding.PEM,s.PrivateFormat.PKCS8,s.NoEncryption()))' '$(CDK_KEY)'; \
	fi || { printf '  cannot generate a key  ·  need openssl, or python3 with the cryptography module\n' >&2; exit 1; }; \
	chmod 600 '$(CDK_KEY)'; \
	printf '  generated  ·  %s\n  Gitignored. Back it up wherever your other secrets live.\n' '$(CDK_KEY)'

## dfu: push a signed image over MCUboot serial recovery  ·  no probe needed
##   Updates the board down the J-Link OB's VCOM, the USB cable already powering
##   it. This is the ONLY over-the-wire update this board has, and that is
##   arithmetic rather than choice: BLE DFU and Matter OTA both stage into a
##   SECOND slot, and a 512 KB part running a 447 KB app has room for exactly
##   one. See firmware/sysbuild.conf.
##   Resets over SWD, so no button is needed. (SW1 DOES reset this board --
##   PSELRESET is programmed to P0.18 -- but the script needs no operator.)
##   NOT RELIABLE: one real upload succeeded and has not reproduced since.
##   scripts/cdk-dfu.sh records everything ruled out.
##   If both the VCOM and the app's own USB enumerate, the guess may pick the
##   wrong one: pass DFU_PORT explicitly and it prints which it used.
##   internal/cdk-dfu-plan.md carries the runbook.
##   Options: DFU_PORT=/dev/cu.usbmodemXXXX  DFU_BAUD=115200  CDK_BUILD=<dir>
dfu:
	@port='$(DFU_PORT)'; \
	if [ -z "$$port" ]; then port=$$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1); fi; \
	if [ -z "$$port" ]; then \
	  printf '  no serial port found  ·  plug the J-Link (J9) in, or pass DFU_PORT=/dev/cu.usbmodemXXXX\n' >&2; \
	  exit 1; \
	fi; \
	$(REPO_ROOT)/scripts/cdk-dfu.sh "$$port" '$(DFU_BAUD)' '$(CDK_SIGNED)' '$(CDK_CHIP)'

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
