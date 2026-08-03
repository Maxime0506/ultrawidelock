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

.PHONY: nrf-build nrf-rebuild nrf-pretty nrf-selftest nrf-flash nrf-flash-erase nrf-term nrf-pairing-code term

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

# Compatibility alias: `term` meant the DK back when the DK was the default
# board. Does the work and says where it went. Not in the help list.
term:
	@printf '  term is now `make nrf-term`  ·  the CDK console is `make monitor`\n'
	@$(MAKE) --no-print-directory nrf-term
