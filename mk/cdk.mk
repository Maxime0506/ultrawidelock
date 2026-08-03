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
#
# SMP=1 adds mcumgr over Bluetooth, which is what nRF Device Manager and nRF
# Connect for iOS speak. It costs 3,712 B of RAM and leaves 2,412 B free on a
# debug build, so it wants RELEASE=1 beside it -- firmware/overlay-smp.conf has
# the measurements and the security note about the unpaired write endpoint.
# Ordered after overlay-release.conf so nothing it sets can be undone by it.
LTO      ?= 1
CDK_LTO  := $(filter-out 0 n no off N NO OFF,$(LTO))
CDK_CONF := overlay-thread.conf$(if $(RELEASE),;overlay-release.conf)$(if $(SMP),;overlay-smp.conf)$(if $(CDK_LTO),;overlay-lto.conf)

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

# ---- delta update over BLE ---------------------------------------------------
# modules/woz_dfu has to reach BOTH images, and they need opposite halves of it:
# the patch APPLIER is compiled into MCUboot, because an application cannot
# rewrite the flash it is executing from, and the RECEIVER is compiled into the
# application, because the bootloader has no radio.
#
# So EXTRA_ZEPHYR_MODULES is set at the SYSBUILD level and not in
# firmware/CMakeLists.txt, whose ZEPHYR_EXTRA_MODULES list is read only by the
# application image -- the bootloader would never see the module at all. What
# carries it across is that sysbuild copies every one of its own cache variables
# into each image's cache file (sysbuild_extensions.cmake:133-147), which is the
# same route NCS uses to get its MCUboot hooks compiled
# (nrf/modules/mcuboot/hooks/).
#
# The two CONFIG_ assignments are per-image and must not be swapped: each half
# is inert without a partition and a peer that the other image owns.
# WOZ_DFU_KEY is the IMAGE-signing key, deliberately. The application checks a
# staged update against its public half, so one secret authorises both what the
# bootloader will boot and what the radio will accept, and they cannot drift.
CDK_DFU  := -DEXTRA_ZEPHYR_MODULES='$(REPO_ROOT)/modules/woz_dfu' \
            -DWOZ_DFU_KEY='$(CDK_KEY)' \
            -Dmcuboot_CONFIG_WOZ_DFU_APPLIER=y \
            -DCONFIG_WOZ_DFU_RECEIVER=y

# DFU_LOG=1 makes the bootloader narrate what it does with a staged patch.
#
# Not on by default and not a size trim: MCUboot here has no LOG, no PRINTK and
# no RTT at all (firmware/sysbuild/mcuboot.conf costs each one), and this turns
# three of them back on. Worth it exactly when the difference between "declined
# the patch" and "applied it and produced an image that fails validation" has to
# be visible, because from the outside those look identical -- both leave an
# erased staging partition and a board that does not run the new firmware.
#
# Read it with MCUboot's OWN elf, not the application's:
#   probe-rs attach --chip nRF52833_xxAA build/<dir>/mcuboot/zephyr/zephyr.elf
# The application re-initialises the RTT control block on every boot
# (CONFIG_SEGGER_RTT_INIT_MODE_ALWAYS in prj.conf, and it has to), so anything
# the bootloader printed is gone the moment the application starts.
CDK_DFU_LOG := $(if $(DFU_LOG),-Dmcuboot_CONFIG_WOZ_DFU_APPLIER_LOG=y \
                               -Dmcuboot_CONFIG_PRINTK=y \
                               -Dmcuboot_CONFIG_USE_SEGGER_RTT=y \
                               -Dmcuboot_CONFIG_RTT_CONSOLE=y)

# ---- over-the-air update -----------------------------------------------------
#
# THE .hex, NOT THE .bin, AND THIS IS NOT A PREFERENCE. The build signs the
# application TWICE, in two separate imgtool runs, and ECDSA signatures are
# randomised -- so zephyr.signed.bin and zephyr.signed.hex carry the same code
# under different signatures, 64 bytes apart at the end. Only the .hex reaches
# merged.hex, so only the .hex is what a flashed board is actually running.
# An update is a DELTA against those exact bytes, so the wrong file produces a
# patch the board declines with "not for this image".
CDK_SIGNED_HEX := $(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.signed.hex
# The old serial-recovery path uploads a whole image and overwrites whatever was
# there, so it does not care which of the two it gets.
CDK_SIGNED := $(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.signed.bin
DFU_BAUD   ?= 115200

# WHAT THE BOARD IS RUNNING, recorded here because a delta cannot be computed
# without it and the board cannot be asked over the air. `flash` and a
# successful `dfu` both write this, so it tracks the board as long as nothing
# else programs it. If it goes stale the update is REFUSED rather than
# mis-applied -- the header carries a CRC of the from-image and the bootloader
# checks it -- so the failure mode is a wasted transfer, not a brick.
CDK_DEPLOYED ?= $(ALIRO_BUILD_ROOT)/cdk-deployed/zephyr.signed.hex
CDK_PATCH    ?= $(CDK_BUILD)/update.wdfu

# The host tooling's Python dependencies, in a throwaway virtualenv rather than
# in the user's interpreter. detools creates the patch, cryptography signs its
# header, bleak carries it over Bluetooth.
CDK_OTA_VENV := $(ALIRO_BUILD_ROOT)/ota-venv
CDK_OTA_PY   := $(CDK_OTA_VENV)/bin/python

# The ELF that goes with $(CDK_DEPLOYED), kept because `ota-window` needs the
# address of a symbol in the image the board is RUNNING, and the build tree only
# ever holds the image being built next. Those differ the moment you rebuild, and
# LTO renames the symbol, so a stale lookup writes a live value into the wrong
# RAM word and the push then fails with access denied for no visible reason.
CDK_DEPLOYED_ELF := $(dir $(CDK_DEPLOYED))zephyr.elf

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
        dfu-serial fota fota-build fota-done fota-confirm ota-patch ota-push ota-smp ota-smp-push ota-smp-list ota-window ota-deps \
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
	  -- -DEXTRA_CONF_FILE="$(CDK_CONF)" -DCONFIG_ALIRO_MATTER_BLE=y \
	     $(CDK_SIGN) $(CDK_DFU) $(CDK_DFU_LOG)
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
	@# Record what the board now runs, so `make dfu` can diff against it. A
	@# delta needs the exact bytes that are on the part, and once the probe is
	@# gone there is no way to ask.
	@if [ -f '$(CDK_SIGNED_HEX)' ]; then \
	  mkdir -p '$(dir $(CDK_DEPLOYED))' && cp '$(CDK_SIGNED_HEX)' '$(CDK_DEPLOYED)'; \
	  cp '$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf' '$(CDK_DEPLOYED_ELF)' 2>/dev/null || true; \
	fi

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

## dfu: update the board over Bluetooth  ·  no cable, no probe
##   Builds the current tree, works out the difference from what the board is
##   already running, signs it, and pushes it. One command, start to finish.
##
##   Press SW2 on the board when it asks. That press is the whole authorization
##   model: the patch is signed and MCUboot re-verifies the RESULT before
##   booting it, so no peer can install code either way -- what a closed window
##   prevents is a stranger in radio range spending your flash's erase cycles
##   and rebooting your lock. The window lasts five minutes.
##
##   Two full slots want 844 KB of a 512 KB part, so what travels is a DELTA:
##   about 7.6 KB between adjacent builds, against a 24.5 KB budget. The board
##   reboots into MCUboot, which applies it in roughly 20-30 seconds.
##
##   Needs to know what the board is running, which it reads from
##   $(CDK_DEPLOYED) -- written by `make flash` and by a successful `make dfu`.
##   If that is missing or stale the board REFUSES the patch rather than
##   mis-applying it, so the cost is a wasted transfer.
##   Options: CDK_BUILD=<dir>  CDK_DEPLOYED=<hex>  OTA_NAME=<advertised name>
dfu:
	@$(MAKE) --no-print-directory build
	@$(MAKE) --no-print-directory ota-patch
	@$(MAKE) --no-print-directory ota-push

## fota: make the file an iPhone can install, and say how  ·  needs SMP=1
##   Builds the tree, diffs it against what the board is running, signs the
##   delta, and dresses it as an MCUboot image so a phone's file picker will
##   accept it. Leaves ONE file to move to the phone and prints the steps.
##
##   The wrapper is why this is a separate target from `dfu`. nRF Device
##   Manager PARSES a file before offering to upload it, and a bare .wdfu has
##   no magic it recognises, so the phone refuses it at the picker. The wrapped
##   file is a genuinely well-formed image -- real sizes, a real SHA-256 -- it
##   is simply not bootable, and nothing ever asks it to be. The board spots
##   the wrapper by its magic and steps over it.
##
##   SETS SMP=1 RELEASE=1 ITSELF, and builds in its own directory. Those are
##   not preferences here: a board without SMP does not speak mcumgr at all, so
##   a patch built without it would take the phone's own transport away, and
##   RELEASE is what leaves the RAM to run it (2,412 B free without, 9,516 B
##   with). Inheriting a bare `make`'s defaults would quietly build the wrong
##   image and diff the board against it, so this target does not inherit them.
##   Options: CDK_FOTA_BUILD=<dir>  CDK_DEPLOYED=<hex>  FOTA_VERSION=<x.y.z>
FOTA_VERSION   ?= 1.0.0
CDK_FOTA_BUILD ?= $(ALIRO_BUILD_ROOT)/cdk-smp-img
CDK_FOTA       := $(CDK_BUILD)/openaliro-fota.bin

fota:
	@$(MAKE) --no-print-directory fota-build \
	  SMP=1 RELEASE=1 CDK_BUILD='$(CDK_FOTA_BUILD)'

fota-build:
	@$(MAKE) --no-print-directory build
	@$(MAKE) --no-print-directory ota-patch
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_patch.py wrap '$(CDK_PATCH)' \
	  --version '$(FOTA_VERSION)' --out '$(CDK_FOTA)'
	@printf '\n  ---- put this on the phone ----------------------------------\n\n'
	@printf '  %s\n\n' '$(CDK_FOTA)'
	@printf '  1. AirDrop that file to the phone, or drop it in Files\n'
	@printf '  2. Press SW2 on the board  (or Apple Home -> Turn On Pairing Mode)\n'
	@printf '  3. nRF Device Manager -> connect to "openaliro"\n'
	@printf '  4. Images tab -> SELECT FILE -> that file -> UPLOAD\n'
	@printf '  5. Device tab -> Reset,  then wait ~30 s\n\n'
	@printf '  Use the Images tab, NOT the guided firmware-upgrade wizard: that\n'
	@printf '  flow waits for a second image to confirm and a reconnect that the\n'
	@printf '  bootloader apply outlasts.\n\n'
	@printf '  6. back here, run  make fota-done\n\n'
	@printf '  Step 6 is not optional. A delta is computed against the exact bytes\n'
	@printf '  on the board, and only this machine keeps the record of what those\n'
	@printf '  are -- a push from the phone is invisible to it. Skip step 6 and the\n'
	@printf '  NEXT update is built from the wrong base and the board refuses it.\n\n'
	@printf '  The window closes after five minutes. Reset is refused outside it\n'
	@printf '  unless a patch is already staged, which is deliberate -- otherwise\n'
	@printf '  anyone in radio range could reboot the lock in a loop.\n\n'

## fota-done: after a phone push, confirm it landed and record what the board runs
##   ASKS THE BOARD rather than assuming. It reads the image list over BLE and
##   compares the hash against the image the last `make fota` built; the record
##   moves only if they match, so a failed or half-finished update leaves the
##   old base in place instead of poisoning the next delta.
fota-done:
	@$(MAKE) --no-print-directory fota-confirm \
	  SMP=1 RELEASE=1 CDK_BUILD='$(CDK_FOTA_BUILD)'

fota-confirm: $(CDK_OTA_PY)
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_smp.py \
	  --expect '$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.signed.bin' \
	  $(if $(OTA_NAME),--name '$(OTA_NAME)')
	@mkdir -p '$(dir $(CDK_DEPLOYED))'
	@cp '$(CDK_SIGNED_HEX)' '$(CDK_DEPLOYED)'
	@cp '$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf' '$(CDK_DEPLOYED_ELF)'
	@printf '  recorded as deployed  ·  %s\n' '$(CDK_DEPLOYED)'

## ota-patch: build a signed delta from the deployed image to the built one
##   Leaves it at $(CDK_PATCH). Useful on its own when the board is elsewhere.
ota-patch: $(CDK_OTA_PY)
	@if [ ! -f '$(CDK_DEPLOYED)' ]; then \
	  printf '  no record of what the board is running  ·  %s\n' '$(CDK_DEPLOYED)' >&2; \
	  printf '  A delta needs the image it starts from. Either `make flash` once over SWD\n' >&2; \
	  printf '  to set the record, or point CDK_DEPLOYED at the signed .hex it is running.\n' >&2; \
	  exit 1; \
	fi
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_patch.py build \
	  --from '$(CDK_DEPLOYED)' --to '$(CDK_SIGNED_HEX)' \
	  --build-dir '$(CDK_BUILD)' --key '$(CDK_KEY)' --out '$(CDK_PATCH)'

## ota-push: send an already-built patch over Bluetooth
##   Waits for you to press SW2, then transfers. On success it records the new
##   image as deployed, so the next `make dfu` diffs from the right place.
ota-push: $(CDK_OTA_PY)
	@test -f '$(CDK_PATCH)' || { printf '  no patch at %s  ·  run `make ota-patch`\n' '$(CDK_PATCH)' >&2; exit 1; }
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_push.py '$(CDK_PATCH)' \
	  $(if $(OTA_NAME),--name '$(OTA_NAME)')
	@mkdir -p '$(dir $(CDK_DEPLOYED))' && cp '$(CDK_SIGNED_HEX)' '$(CDK_DEPLOYED)'
	@cp '$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf' '$(CDK_DEPLOYED_ELF)' 2>/dev/null || true
	@printf '  recorded as deployed  ·  %s\n' '$(CDK_DEPLOYED)'

## ota-smp: push the patch over mcumgr instead, exactly as a phone would
##
##   Needs a board built with SMP=1. Sends the same bytes as nRF Device Manager,
##   so it answers the one question the phone cannot: whether a failure is in
##   the firmware or in the app. `make ota-smp-list` just reads the image list,
##   which is the cheapest proof that group 1 is answering at all.
#
# SETS ITS OWN CONFIGURATION, like `fota` and for the same reason: this target
# is definitionally the mcumgr path, and a board without SMP does not speak it
# at all. Inheriting a bare `make`'s defaults would look in build/cdk-matter --
# a different, non-SMP image -- and either find no patch or push one built from
# the wrong base. RELEASE goes with it because SMP does not fit without it.
ota-smp:
	@$(MAKE) --no-print-directory ota-smp-push \
	  SMP=1 RELEASE=1 CDK_BUILD='$(CDK_FOTA_BUILD)'

ota-smp-push: $(CDK_OTA_PY)
	@test -f '$(CDK_PATCH)' || { printf '  no patch at %s  ·  run `make fota`\n' '$(CDK_PATCH)' >&2; exit 1; }
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_smp.py '$(CDK_PATCH)' \
	  $(if $(OTA_NAME),--name '$(OTA_NAME)')
	@mkdir -p '$(dir $(CDK_DEPLOYED))' && cp '$(CDK_SIGNED_HEX)' '$(CDK_DEPLOYED)'
	@cp '$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf' '$(CDK_DEPLOYED_ELF)' 2>/dev/null || true
	@printf '  recorded as deployed  ·  %s\n' '$(CDK_DEPLOYED)'

ota-smp-list: $(CDK_OTA_PY)
	@$(CDK_OTA_PY) $(REPO_ROOT)/scripts/woz_smp.py --list \
	  $(if $(OTA_NAME),--name '$(OTA_NAME)')

## ota-window: open the update window over SWD instead of pressing SW2
##   BENCH ONLY, and it needs the probe the whole point of this is to avoid. It
##   exists because an automated test cannot press a button: it writes the
##   window flag straight into RAM. The symbol is LTO-renamed, so it is looked
##   up in the ELF rather than hardcoded.
##   POINT CDK_BUILD AT THE IMAGE THE BOARD IS RUNNING, not the one being
##   pushed. The address comes out of that ELF, and writing it into the wrong
##   RAM location does nothing visible -- the push simply keeps waiting.
ota-window:
	@elf='$(CDK_DEPLOYED_ELF)'; \
	[ -f "$$elf" ] || elf='$(CDK_BUILD)/$(CDK_IMAGE)/zephyr/zephyr.elf'; \
	nm=$$(ls /opt/nordic/ncs/toolchains/*/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm 2>/dev/null | head -1); \
	addr=$$($$nm "$$elf" | awk '$$3 ~ /^s_open(\.|$$)/ { print $$1; exit }'); \
	if [ -z "$$addr" ]; then printf '  cannot find s_open in %s\n' "$$elf" >&2; exit 1; fi; \
	printf '  opening the update window by writing s_open at 0x%s\n' "$$addr"; \
	probe-rs write --chip $(CDK_CHIP) b8 "0x$$addr" 1

## ota-deps: create the host virtualenv the update tooling runs in
ota-deps: $(CDK_OTA_PY)
$(CDK_OTA_PY):
	@printf '  creating the update tooling virtualenv  ·  %s\n' '$(CDK_OTA_VENV)'
	@python3 -m venv '$(CDK_OTA_VENV)'
	@'$(CDK_OTA_VENV)/bin/pip' install --quiet --disable-pip-version-check \
	  detools cryptography bleak
	@printf '  ready  ·  detools, cryptography, bleak\n'

## dfu-serial: the old serial-recovery upload  ·  kept, but it does not work
##   Uploads a whole image down the J-Link OB's VCOM. One transfer succeeded on
##   2026-08-02 and it has never reproduced: MCUboot enters its listening window
##   with a full four seconds available and still does not answer mcumgr.
##   scripts/cdk-dfu.sh records everything ruled out. `make dfu` is the path
##   that works.
##   Options: DFU_PORT=/dev/cu.usbmodemXXXX  DFU_BAUD=115200  CDK_BUILD=<dir>
dfu-serial:
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
