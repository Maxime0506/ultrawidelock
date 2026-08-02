# mk/nrf5340dk.mk — the nRF5340 DK port: NFC tap + approach unlock.
#
# The board this project was brought up on and the only one with NFC. Thin front
# door over scripts/build-nrf5340dk.sh, which holds the real logic: preflight,
# pristine-vs-incremental signature detection, chip resolution.
#
# Every target here is nrf-prefixed. Bare build/flash/monitor mean the
# DWM3001CDK (mk/cdk.mk).

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
  $(if $(CIR),CIR=$(CIR)))

NRF_BUILD_SH := $(REPO_ROOT)/scripts/build-nrf5340dk.sh

.PHONY: nrf-build nrf-rebuild nrf-pretty nrf-selftest nrf-flash nrf-flash-erase nrf-term term

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

## nrf-flash: app-only flash
nrf-flash:
	@$(NRF_ENV) $(NRF_BUILD_SH) flash

## nrf-flash-erase: full erase + flash  ·  needed after a net-core change
nrf-flash-erase:
	@$(NRF_ENV) $(NRF_BUILD_SH) flash-erase

## nrf-term: serial console — live logs + typeable shell (tio, 115200 8N1)
##   Auto-detects the nRF5340DK console (VCOM1 — this firmware's console + Zephyr
##   shell live there; VCOM0 is silent).  ctrl-t q quits.  `help` lists commands.
##   The DWM3001CDK has no UART console; use `make monitor` (RTT) for that board.
##   Override: make nrf-term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
nrf-term:
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
