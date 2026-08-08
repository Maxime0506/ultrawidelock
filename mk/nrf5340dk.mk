# mk/nrf5340dk.mk — the nRF5340 DK port: NFC tap + approach unlock.
#
# The board this project was brought up on and the only one with NFC. Thin front
# door over scripts/build-nrf5340dk.sh, which holds the real logic: preflight,
# pristine-vs-incremental signature detection, chip resolution.
#
# Every target here is nrf-prefixed. Bare build/flash/monitor mean the
# DWM3001CDK (mk/cdk.mk).

# LTO and DFU are both ON by default, `LTO=0` / `DFU=0` opt out (also n/no/off).
#
# Both defaults are resolved with $(origin) rather than `?=`, because make
# variables are global across the includes and `LTO ?= 1` here would also hand
# mk/cdk.mk a value it is trying to decide for itself. $(origin) distinguishes
# "the user said nothing" from an explicit LTO=1, so each board keeps its own
# default while sharing one spelling of the option.
#
# LTO earned this on 2026-08-03: a DFU=1 LTO=1 image commissioned into Apple Home
# and then did approach unlock, NFC tap and Home-tile lock/unlock, which is the
# ranging path and the tap path together. Worth 77,452 B of flash. See
# overlays/lto.conf; re-run that walk-up after any change that could move the
# ranging path.
#
# DFU rides the same run, and LTO is what pays for it: MCUboot costs 33,280 B of
# app partition and the OTA code another 32,812 B, against LTO's 77,452 B, so the
# pair still leaves more free flash than the old no-bootloader default did.
#
# WHAT THE DEFAULT CARRIES, and the one part of it that is still unfixed:
#   * `make dfu-key` is now a prerequisite of this board too, not just the CDK.
#     The bootloader signs with this checkout's key and the build refuses to run
#     without one (scripts/check-signing-key.sh), rather than falling back to
#     MCUboot's PUBLISHED demo key the way it did when DFU=1 first landed.
#     One key serves both boards; the top-level Makefile owns the path.
#   * The flash map moves external_nvs from 0x0 to 0x12f000, so a board carrying
#     the old no-bootloader layout loses its Aliro reader storage the first time
#     it takes this build. `DFU=0` keeps the old layout.
NRF_LTO := $(filter-out 0 n no off N NO OFF,$(if $(filter undefined,$(origin LTO)),1,$(LTO)))
NRF_DFU := $(filter-out 0 n no off N NO OFF,$(if $(filter undefined,$(origin DFU)),1,$(DFU)))

# Assemble the env prefix from whichever options were set.
NRF_ENV := $(strip \
  $(if $(CHIP),UWB_CHIP=$(CHIP)) \
  $(if $(PRETTY),PRETTY=$(PRETTY)) \
  $(if $(PRISTINE),PRISTINE=$(PRISTINE)) \
  $(if $(SELFTEST),UWB_SELFTEST=$(SELFTEST)) \
  $(if $(STRICT),STRICT=$(STRICT)) \
  $(if $(HA),HA=$(HA)) \
  $(if $(ALIRO_SOURCE),ALIRO_SOURCE=$(ALIRO_SOURCE)) \
  $(if $(ALIRO_TRACE),ALIRO_TRACE=$(ALIRO_TRACE)) \
  $(if $(NFC),NFC=$(NFC)) \
  $(if $(NRF_LTO),LTO=1) \
  $(if $(NRF_DFU),DFU=1) \
  $(if $(CIR),CIR=$(CIR)))

NRF_BUILD_SH := $(REPO_ROOT)/scripts/build-nrf5340dk.sh

# Where the Matter onboarding payload lands. Generated at BUILD time
# (CONFIG_CHIP_FACTORY_DATA_GENERATE_ONBOARDING_CODES) and merged into the image
# (CONFIG_CHIP_FACTORY_DATA_MERGE_WITH_FIRMWARE), so it describes the hex that was
# built here, not whatever is on the board. Mirrors build-nrf5340dk.sh's own
# ALIRO_SOURCE split so the code shown belongs to the variant you built.
NRF_BUILD_DIR := $(ALIRO_BUILD_ROOT)/nrf5340dk$(if $(filter 0,$(ALIRO_SOURCE)),-blob)
NRF_FACTORY   := $(NRF_BUILD_DIR)/matter-aliro-door-lock-app/zephyr/factory_data.txt

# The Aliro initiator is a second application on the same board, so it gets its
# own build directory rather than sharing the door lock's.
NRF_INIT_BUILD := $(ALIRO_BUILD_ROOT)/nrf5340dk-initiator

# The initiator is a plain Zephyr app, so it is built with west directly rather
# than through scripts/build-nrf5340dk.sh, whose do_build resolves door-lock
# overlays, UWB chip variants and DFU flags that this application has none of.
#
# The cost of not using the script is that this does NOT reproduce its workspace
# resolution, which auto-seeds a per-worktree copy when ./workspace is absent
# (see its header, and `make ws-seed`). A worktree that has never built anything
# has no ./workspace and this target will fail; run `make bootstrap` or
# `make ws-seed` first. Everything else the script does is door-lock specific.
NRF_WS     := $(REPO_ROOT)/workspace
NRF_LAUNCH := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VER) --

NRF_RELEASE_OUT ?= $(ALIRO_BUILD_ROOT)/release/openaliro-nrf5340dk
NRF_RELEASE_OUT := $(abspath $(NRF_RELEASE_OUT))
NRF_RELEASE_STAGE := $(ALIRO_BUILD_ROOT)/release/.nrf-stage
NRF_RELEASE_VER ?= $(shell git -C $(REPO_ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown)
# Exported for the same reason as ESP_RELEASE_VER in mk/esp32.mk: a tag reaches
# the recipe through the environment rather than being pasted into '...' at Make
# time, where an apostrophe in it would end the quoting.
export NRF_RELEASE_VER

.PHONY: nrf-build nrf-rebuild nrf-pretty nrf-selftest nrf-flash nrf-flash-erase nrf-init-build nrf-init-flash nrf-term nrf-pairing-code nrf-release term

##@ nRF5340 DK  ·  NFC tap + approach unlock
## nrf-build: incremental build          -> build/nrf5340dk/merged.hex
##   ALIRO_SOURCE=0 builds into build/nrf5340dk-blob instead, so flipping
##   between the two implementations no longer forces a pristine rebuild.
##   Options: CHIP=dw3720 (default dw3000)  PRETTY=1  PRISTINE=1  SELFTEST=1
##            STRICT=1 (drop suspect ranges)
##            HA=1 (Home Assistant variant — needs `make bootstrap HA=1` too)
##            ALIRO_SOURCE=0 (legacy Nordic binary fallback)
##            ALIRO_TRACE=1 (currently blocked: vendor trace patch is absent)
##            CIR=1 (CIA/CIR diagnostics: `aliro cir on|dump on|probe`)
##            NFC=pn532|st25r|none (reader transport; default st25r)
##            LTO=0 (opt OUT of link-time optimisation, which is ON by default
##                   and worth 77,452 B. Use it when a stack trace has to name
##                   every frame. Forces a pristine rebuild.)
##            DFU=0 (opt OUT of MCUboot + Matter OTA, which are ON by default.
##                   DFU=0 gives the old no-bootloader bench layout, which keeps
##                   33,280 B of app flash and leaves external_nvs at 0x0.
##                   NOTE the default needs `make dfu-key` first, and moves
##                   external_nvs, which costs an already provisioned board its
##                   reader storage.)
##   e.g.     make nrf-build PRETTY=1 CHIP=dw3720
nrf-build:
	@$(NRF_ENV) $(NRF_BUILD_SH) build

## nrf-rebuild: force clean pristine build
nrf-rebuild:
	@$(NRF_ENV) $(NRF_BUILD_SH) rebuild

## nrf-selftest: one-shot boot self-test (no iPhone)
nrf-selftest:
	@$(NRF_ENV) UWB_SELFTEST=1 $(NRF_BUILD_SH) build

## nrf-pretty: build with curated / quiet console
nrf-pretty:
	@$(NRF_ENV) PRETTY=1 $(NRF_BUILD_SH) build

## nrf-pairing-code: this image's Matter pairing code + QR  ·  run by nrf-term
##   Read out of the build, not off the board: the payload is generated at build
##   time and merged into the hex, so it is a property of what you built. If you
##   flashed a different variant since, rebuild that one to see its code.
##   The values come from CONFIG_CHIP_DEVICE_{DISCRIMINATOR,SPAKE2_PASSCODE} and
##   are fixed in Kconfig rather than random per build, so this is a bench
##   credential, not a per-device secret.
nrf-pairing-code:
	@f='$(NRF_FACTORY)'; \
	if [ ! -f "$$f" ]; then \
	  printf '  %sno pairing code yet%s  ·  build first: make nrf-build\n' \
	    "$$(tput setaf 3 2>/dev/null)" "$$(tput sgr0 2>/dev/null)"; \
	  printf '    looked in %s\n' "$$f"; \
	  exit 0; \
	fi; \
	m=$$(sed -n 's/^Manualcode[[:space:]]*:[[:space:]]*//p' "$$f" | tr -d '[:space:]'); \
	q=$$(sed -n 's/^QRCode[[:space:]]*:[[:space:]]*//p' "$$f" | tr -d '[:space:]'); \
	b=$$(tput bold 2>/dev/null); r=$$(tput sgr0 2>/dev/null); d=$$(tput dim 2>/dev/null); \
	printf '\n  %sMatter pairing%s\n' "$$b" "$$r"; \
	if [ -n "$$m" ]; then \
	  g=$$(printf '%s' "$$m" | sed -E 's/^([0-9]{4})([0-9]{3})([0-9]{4})$$/\1-\2-\3/'); \
	  printf '    manual code  %s%s%s   %sraw %s%s\n' "$$b" "$$g" "$$r" "$$d" "$$m" "$$r"; \
	fi; \
	[ -n "$$q" ] && printf '    QR payload   %s\n' "$$q"; \
	[ -f "$${f%.txt}.png" ] && printf '    %sQR image     %s%s\n' "$$d" "$${f%.txt}.png" "$$r"; \
	printf '\n'

## nrf-flash: app-only flash
nrf-flash:
	@$(NRF_ENV) $(NRF_BUILD_SH) flash

## nrf-flash-erase: full erase + flash  ·  needed after a net-core change
nrf-flash-erase:
	@$(NRF_ENV) $(NRF_BUILD_SH) flash-erase

## nrf-init-build: build the Aliro initiator      -> build/nrf5340dk-initiator
##   Options: PRISTINE=1 (required after adding or changing boards/*.overlay --
##            Zephyr caches DTC_OVERLAY_FILE, so a plain rebuild silently ignores
##            a newly added board overlay. See commit c6912ba2.)
##   A separate application from `make nrf-build`, which builds the door lock.
nrf-init-build:
	@cd '$(NRF_WS)' && $(NRF_LAUNCH) west build \
	  -b nrf5340dk/nrf5340/cpuapp --sysbuild \
	  $(if $(PRISTINE),-p always) \
	  -d '$(NRF_INIT_BUILD)' '$(REPO_ROOT)/ports/nrf5340dk/initiator' \
	  -- -DZEPHYR_EXTRA_MODULES='$(REPO_ROOT)/modules/woz_uwb;$(REPO_ROOT)/modules/woz_dw3000'

## nrf-init-flash: flash the Aliro initiator (both cores) -> build/nrf5340dk-initiator
##   A different application from the one every other nrf- target builds: the DK
##   standing in for an iPhone, from ports/nrf5340dk/initiator. `make nrf-flash`
##   would put the door-lock image on the same board instead.
##   Erase + flash, because the initiator is a sysbuild pair and the net core
##   carries the BLE controller; an app-only write leaves whichever controller
##   was already there. domains.yaml flashes ipc_radio first, then the app.
##   The build itself is not driven from here yet; build it with
##   `west build -b nrf5340dk/nrf5340/cpuapp --sysbuild -d $(NRF_INIT_BUILD) ports/nrf5340dk/initiator`.
nrf-init-flash:
	@[ -f '$(NRF_INIT_BUILD)/build.ninja' ] || { \
	  printf '  no initiator build at %s  ·  build it first\n' '$(NRF_INIT_BUILD)' >&2; exit 1; }
	@ALIRO_BUILD='$(NRF_INIT_BUILD)' $(NRF_BUILD_SH) flash-erase

## nrf-term: serial console — live logs + typeable shell (tio, 115200 8N1)
##   Prints this image's Matter pairing code first, because that is what you came
##   for and the console itself may have nothing to say: the Matter build has no
##   log backend (see below), so an empty terminal is the expected result rather
##   than a fault.
##   Auto-detects the nRF5340DK console (VCOM1 — this firmware's console + Zephyr
##   shell live there; VCOM0 is silent).  ctrl-t q quits.  `help` lists commands.
##   The DWM3001CDK has no UART console; use `make monitor` (RTT) for that board.
##   Override: make nrf-term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
nrf-term: nrf-pairing-code
	@command -v tio >/dev/null 2>&1 || { printf '  tio not found  ·  install: brew install tio\n' >&2; exit 1; }
	@port='$(PORT)'; \
	if [ -z "$$port" ]; then \
	  port=$$(ioreg -l -w0 2>/dev/null \
	    | awk '/kUSBSerialNumberString/{s=$$0;sub(/.*= "/,"",s);sub(/".*/,"",s);serial=s} /IOCalloutDevice/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c);print serial"\t"c}' \
	    | sort \
	    | awk -F'\t' '{cnt[$$1]++; if($$2>max[$$1])max[$$1]=$$2} END{best="";bc=-1; for(x in cnt) if(cnt[x]>bc||(cnt[x]==bc&&x<best)){bc=cnt[x];best=x} if(best!="")print max[best]}'); \
	fi; \
	if [ -z "$$port" ]; then \
	  printf '  no serial port found  ·  plug in the board or pass PORT=/dev/cu.usbmodemXXXX\n' >&2; \
	  ls /dev/cu.usbmodem* 2>/dev/null | sed 's/^/    candidate: /' >&2; \
	  exit 1; \
	fi; \
	logargs=; [ -n '$(LOG)' ] && logargs='-L --log-file $(LOG)'; \
	printf '  tio %s  @ %s 8N1  ·  logs + shell (type help)  ·  ctrl-t q to quit\n' "$$port" '$(BAUD)'; \
	exec tio -b $(BAUD) $$logargs "$$port"

## nrf-release: build and bundle the DK image to publish
##   The same folder shape every target ships, assembled by
##   scripts/release-bundle.sh: both core hex files, the flashing script, the
##   guide, README.txt, VERSION.txt and SHA256SUMS.txt. Build it locally to see
##   exactly what a stranger downloads.
##
##   DFU IS DELIBERATELY OFF HERE, unlike `make nrf-build`, and this target says
##   so rather than inheriting it. MCUboot needs a signing key that only this
##   checkout holds (scripts/check-signing-key.sh refuses without one), and no
##   release key exists for this board the way it does for the CDK. So the
##   published DK image has no bootloader and no Matter OTA, which is what CI
##   has always shipped by calling build-nrf5340dk.sh directly. Turning it on is
##   a key-management decision, not a build flag.
##   Options: NRF_RELEASE_OUT=<dir>  NRF_RELEASE_VER=<tag>
nrf-release:
	@# Recursive rather than a DFU=0 prefix on NRF_BUILD_SH: NRF_ENV already
	@# carries DFU=1 by default, and a second assignment on the same command
	@# line is won by the later one, so prefixing quietly built a bootloader.
	@$(MAKE) --no-print-directory nrf-build DFU=0
	@# The onboarding payload is generated at build time and merged into the
	@# hex, so the code shipped in VERSION.txt describes the image beside it.
	@# The QR image rides along when the build produced one: scanning a PNG out
	@# of the zip beats reading a code off a serial console.
	@f='$(NRF_FACTORY)'; \
	  code=$$(sed -n 's/^Manualcode[[:space:]]*:[[:space:]]*//p' "$$f" 2>/dev/null | tr -d '[:space:]'); \
	  qr=; \
	  if [ -f "$${f%.txt}.png" ]; then \
	    mkdir -p '$(NRF_RELEASE_STAGE)'; \
	    cp "$${f%.txt}.png" '$(NRF_RELEASE_STAGE)/SETUP-QR.png'; \
	    qr='$(NRF_RELEASE_STAGE)/SETUP-QR.png'; \
	  fi; \
	  $(REPO_ROOT)/scripts/release-bundle.sh \
	    --target nrf5340dk --out '$(NRF_RELEASE_OUT)' \
	    --version "$$NRF_RELEASE_VER" \
	    --board 'nRF5340 DK + DWM3000EVB + X-NUCLEO-NFC12A1' \
	    $${code:+--setup-code "$$code"} \
	    --commission-note 'Type this into Apple Home, or scan SETUP-QR.png in this folder. Both also print on the serial console at 115200 baud.' \
	    '$(NRF_BUILD_DIR)/merged.hex' '$(NRF_BUILD_DIR)/merged_CPUNET.hex' $$qr

# Compatibility alias: `term` meant the DK back when the DK was the default
# board. Does the work and says where it went. Not in the help list.
term:
	@printf '  term is now `make nrf-term`  ·  the CDK console is `make monitor`\n'
	@$(MAKE) --no-print-directory nrf-term
