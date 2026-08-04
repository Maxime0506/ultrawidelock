# mk/esp32.mk — the ESP32 ports: matter-lock, reader, initiator.
#
# One implementation for all three apps, selected by APP=. The app Makefiles
# under ports/esp32/apps/*/ are thin forwarders into this file, so every command
# their READMEs document keeps working and the serial-port guard below exists in
# exactly one place.
#
#   make esp-build APP=matter-lock TARGET=esp32s3
#   make esp-flash APP=reader
#   cd ports/esp32/apps/reader && make flash      # same thing, via the forwarder
#
# No ESP-IDF or esp-matter version is pinned anywhere in the repo; the paths
# below are the only thing this file fixes, and both are overridable.

APP     ?= matter-lock
TARGET  ?= esp32s3
# VARIANT layers an extra sdkconfig fragment and, because it is part of the
# build directory name, gets its own tree and its own sdkconfig. That is the
# whole point: switching variants used to mean reconfiguring one directory, so a
# build that once passed a fragment kept using it and dropping the variant
# silently kept producing the variant image.
#   presence  the signed-presence console command   (matter-lock, reader)
#   hamqtt    Home Assistant MQTT + mbedTLS sizing  (matter-lock)
#   piv       PIN-enforced PIV recovery build       (matter-lock)
VARIANT ?=

ESP_APP_DIR := $(REPO_ROOT)/ports/esp32/apps/$(APP)
ESP_BUILD   := $(ALIRO_BUILD_ROOT)/esp32-$(APP)-$(TARGET)$(if $(VARIANT),-$(VARIANT))
# The generated config belongs to the build, not to the checkout. Keyed by
# APP+TARGET+VARIANT along with the tree, so building the C6 can no longer
# overwrite the config the hardware-validated S3 image was built from.
ESP_SDKCONFIG := $(ESP_BUILD)/sdkconfig

# ESP-IDF + esp-matter environment. Override if installed elsewhere:
#   make esp-build IDF_EXPORT=/path/to/esp-idf/export.sh
IDF_EXPORT      ?= $(HOME)/esp/esp-idf/export.sh
ESP_MATTER_PATH ?= $(HOME)/esp/esp-matter
# make esp-flash FORCE=1 stops whatever holds the UART (a monitor/tio) first.
FORCE ?=
# presence-clone / presence-enroll arguments.
SRC  ?=
NAME ?=

PRESENCE := $(REPO_ROOT)/tools/presence_git.py

# Only matter-lock needs esp-matter; sourcing it for the others would demand an
# install they do not use. Both exports are noisy on success, so their output is
# dropped; failures still show because the idf.py call after them fails loudly.
ifeq ($(APP),matter-lock)
ESP_ENV := export ESP_MATTER_PATH="$(ESP_MATTER_PATH)" && . "$(IDF_EXPORT)" >/dev/null 2>&1 && \
           . "$(ESP_MATTER_PATH)/export.sh" >/dev/null 2>&1
else
ESP_ENV := . "$(IDF_EXPORT)" >/dev/null 2>&1
endif

# Every build shares one directory and one sdkconfig, named explicitly so a
# recipe can never pick up whichever config happened to be in the app folder.
# ESP-IDF appends sdkconfig.defaults.$(TARGET) to each entry by itself, so the
# per-target fragments ride along without being listed.
ESP_DEFAULTS := sdkconfig.defaults$(if $(VARIANT),;sdkconfig.$(VARIANT))
ifeq ($(VARIANT),hamqtt)
ESP_DEFAULTS := sdkconfig.defaults;sdkconfig.defaults.hamqtt
endif

# IDF_TARGET is passed on EVERY invocation, not left to a prior `set-target`.
# The generated config lives in the build directory now, so a fresh directory
# starts with no target selected and ESP-IDF silently falls back to plain esp32 --
# which fails deep in the compile as
#   board_pins.h: #error "Unsupported ESP32 target for the DW3000 port"
# rather than as "you forgot set-target". Passing it here makes
# `make esp-build APP=... TARGET=...` correct on its own. Safe to repeat: the
# directory name already encodes the target, so it can never contradict itself.
IDFPY := $(ESP_ENV) && idf.py -B "$(ESP_BUILD)" \
         -D IDF_TARGET="$(TARGET)" \
         -D SDKCONFIG="$(ESP_SDKCONFIG)" \
         -D SDKCONFIG_DEFAULTS="$(ESP_DEFAULTS)"

# USB vendor ids (decimal, as ioreg reports them) used to classify ports.
# Keep values trailing-whitespace-free: an inline comment would leak spaces into
# the value and break the awk string compares below.
# 0x1A86 WCH CH343/CH9102 UART bridge (the ESP devkit); 0x303A Espressif native
# USB (S3 USB-OTG port); 0x1366 SEGGER J-Link = the nRF5340DK, NEVER flash.
VID_WCH       := 6790
VID_ESPRESSIF := 12346
VID_SEGGER    := 4966

# Resolve $$port to a SAFE ESP port, or fail loudly. Refuses SEGGER ports.
define RESOLVE_PORT
port='$(PORT)'; \
table=$$(ioreg -l -w0 2>/dev/null | LC_ALL=C awk '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c);print vend"\t"c}'); \
if [ -n "$$port" ]; then \
	vend=$$(printf '%s\n' "$$table" | awk -F'\t' -v p="$$port" '$$2==p{print $$1}'); \
	if [ "$$vend" = "$(VID_SEGGER)" ]; then echo "  refusing $$port: SEGGER/J-Link (nRF) port — protected rig" >&2; exit 1; fi; \
else \
	port=$$(printf '%s\n' "$$table" | awk -F'\t' '$$1=="$(VID_WCH)"||$$1=="$(VID_ESPRESSIF)"{print $$2; exit}'); \
	if [ -z "$$port" ]; then \
		if printf '%s\n' "$$table" | awk -F'\t' '$$1=="$(VID_SEGGER)"{f=1} END{exit f?0:1}'; then \
			echo "  only nRF J-Link (SEGGER) ports present — plug the ESP into its UART port, or pass PORT=" >&2; \
		else \
			echo "  no ESP serial port found — plug it in or pass PORT=/dev/cu.usbmodemXXXX" >&2; \
		fi; \
		ls /dev/cu.usbmodem* 2>/dev/null | sed 's/^/    seen: /' >&2; \
		exit 1; \
	fi; \
fi
endef

# Flashing needs the UART exclusively. If a monitor/tio holds $$port, refuse with
# a clear message (FORCE=1 stops the holder first). Non-destructive by default.
define ENSURE_PORT_FREE
pids=$$(lsof -t "$$port" 2>/dev/null | tr '\n' ' '); \
busy="$$pids"; \
if [ -z "$$busy" ] && command -v python3 >/dev/null 2>&1; then \
	python3 -c "import fcntl,os,sys;fd=os.open(sys.argv[1],os.O_RDWR|os.O_NONBLOCK);fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)" "$$port" >/dev/null 2>&1 \
		|| busy="held under an exclusive lock (owner not named by lsof)"; \
fi; \
if [ -n "$$busy" ]; then \
	if [ "$(FORCE)" = "1" ] && [ -n "$$pids" ]; then \
		echo "  FORCE: stopping port holder(s): $$pids"; \
		kill $$pids 2>/dev/null || true; \
		sleep 1; \
	elif [ "$(FORCE)" = "1" ]; then \
		echo "  FORCE: $$port is $$busy and has no PID to stop; trying anyway" >&2; \
	else \
		echo "  $$port is busy: $$busy" >&2; \
		echo "  quit it (idf.py monitor: ctrl-]  ·  tio: ctrl-t q) or run: make esp-flash FORCE=1" >&2; \
		exit 1; \
	fi; \
fi
endef

# Fail before idf.py does, naming the variable that fixes it.
define ESP_CHECK_ENV
@[ -d "$(ESP_APP_DIR)" ] || { echo "  no such app: APP=$(APP)  ·  try matter-lock, reader or initiator" >&2; exit 1; }
@[ -f "$(IDF_EXPORT)" ] || { echo "  ESP-IDF export.sh not found at $(IDF_EXPORT)" >&2; \
  echo "  set it: make <target> IDF_EXPORT=/path/to/esp-idf/export.sh" >&2; exit 1; }
endef
ifeq ($(APP),matter-lock)
define ESP_CHECK_MATTER
@[ -f "$(ESP_MATTER_PATH)/export.sh" ] || { echo "  esp-matter not found at $(ESP_MATTER_PATH)" >&2; \
  echo "  set it: make <target> ESP_MATTER_PATH=/path/to/esp-matter" >&2; exit 1; }
endef
else
ESP_CHECK_MATTER :=
endif

# Publishing. Every chip the browser flasher's manifest lists, because the two
# have to agree: a manifest offering a chip the release does not carry is a
# download that 404s inside somebody's browser.
ESP_RELEASE_CHIPS ?= esp32s3 esp32c5 esp32c6
ESP_RELEASE_OUT   ?= $(ALIRO_BUILD_ROOT)/release/openaliro-esp32-matter-lock
ESP_RELEASE_OUT   := $(abspath $(ESP_RELEASE_OUT))
ESP_RELEASE_STAGE := $(ALIRO_BUILD_ROOT)/release/.esp-stage
ESP_RELEASE_VER   ?= $(shell git -C $(REPO_ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown)
# Exported so the recipes below can read it as "$$ESP_RELEASE_VER" instead of
# pasting it in at Make time. A tag is attacker-influenced text: interpolated
# into '...' a single quote in it ends the quoting and the rest becomes shell.
# Export, not plain reference, because Make does not put ordinary variables in a
# recipe's environment -- in CI the workflow sets it and ?= keeps that value, on
# a bench `git describe` fills it, and both then reach the shell the same way.
export ESP_RELEASE_VER

.PHONY: esp-check-env esp-set-target esp-build esp-rebuild esp-reconfigure \
        esp-merge-bin esp-release \
        esp-menuconfig esp-size esp-flash esp-app-flash esp-flash-erase \
        esp-monitor esp-go esp-run esp-term esp-lab esp-ports esp-clean esp-env \
        esp-presence-on esp-presence-off esp-presence-flash esp-presence-clone \
        esp-presence-probe esp-presence-enroll esp-presence-sign esp-help

esp-check-env:
	$(ESP_CHECK_ENV)
	$(ESP_CHECK_MATTER)

##@ ESP32  ·  APP=matter-lock|reader|initiator  TARGET=esp32s3|esp32c5|esp32c6
## esp-set-target: regenerate this build's sdkconfig from scratch for TARGET
##   Not a prerequisite: esp-build passes IDF_TARGET itself, and each APP/TARGET
##   pair has its own directory and sdkconfig. Use this to discard a config you
##   have edited (esp-menuconfig, esp-presence-on) and start from the defaults.
esp-set-target: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) set-target $(TARGET)

## esp-build: compile the app   -> build/esp32-<app>-<target>[-<variant>]
##   Options: APP=  TARGET=  VARIANT=presence|hamqtt|piv
##            IDF_EXPORT=  ESP_MATTER_PATH=
esp-build: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) build

## esp-rebuild: clean, then a full rebuild
esp-rebuild: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) fullclean && $(IDFPY) build

## esp-flash: build + write bootloader + partition table + app
##   Auto-selects the ESP's USB-UART port and REFUSES any SEGGER/J-Link port
##   (VID 0x1366), so this can never touch the nRF rig. FORCE=1 stops a holder.
esp-flash: esp-check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  flashing $$port"; \
	cd "$(ESP_APP_DIR)" && $(IDFPY) -p "$$port" flash

## esp-app-flash: build + write only the app  (fast iteration)
esp-app-flash: esp-check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  app-flashing $$port"; \
	cd "$(ESP_APP_DIR)" && $(IDFPY) -p "$$port" app-flash

## esp-flash-erase: full chip erase, then flash  (drops commissioning + Aliro NVS)
esp-flash-erase: esp-check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  erasing + flashing $$port"; \
	cd "$(ESP_APP_DIR)" && $(IDFPY) -p "$$port" erase-flash && $(IDFPY) -p "$$port" flash

## esp-monitor: interactive serial console (logs + shell)  ·  ctrl-] to quit
##   Override: make esp-monitor PORT=/dev/cu.usbmodemXXXX BAUD=115200
esp-monitor: esp-check-env
	@$(RESOLVE_PORT); \
	echo "  monitoring $$port @ $(BAUD)  (ctrl-] quits)"; \
	cd "$(ESP_APP_DIR)" && $(IDFPY) -p "$$port" -b $(BAUD) monitor

## esp-go: build + flash + monitor in one shot  (the usual bench loop)
esp-go: esp-check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  build + flash + monitor on $$port"; \
	cd "$(ESP_APP_DIR)" && $(IDFPY) -p "$$port" -b $(BAUD) flash monitor

## esp-run: alias for esp-go
esp-run: esp-go

## esp-menuconfig: interactive sdkconfig editor
esp-menuconfig: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) menuconfig

## esp-reconfigure: re-run cmake after editing CMakeLists / sdkconfig / adding files
esp-reconfigure: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) reconfigure

## esp-size: binary size + partition headroom
esp-size: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) size

## esp-merge-bin: fuse bootloader + partition table + app into one 0x0 image
##   What the release and the browser flasher both ship, so a user writes one
##   file at one offset instead of three at three.
esp-merge-bin: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) merge-bin -o openaliro-$(APP)-$(TARGET).bin
	@printf '  merged  ·  %s/openaliro-%s-%s.bin\n' '$(ESP_BUILD)' '$(APP)' '$(TARGET)'

## esp-release: build every chip and bundle the images to publish
##   Recursive per chip on purpose: ESP_BUILD and the sdkconfig path are fixed
##   when this file is parsed, so one recipe cannot retarget itself, and each
##   chip needs its own build directory anyway (set-target wipes both).
##   Options: ESP_RELEASE_CHIPS='esp32s3 ...'  ESP_RELEASE_OUT=  ESP_RELEASE_VER=
esp-release:
	@for t in $(ESP_RELEASE_CHIPS); do \
	  printf '\n  building matter-lock for %s\n' "$$t"; \
	  $(MAKE) --no-print-directory esp-build APP=matter-lock TARGET="$$t" || exit 1; \
	  $(MAKE) --no-print-directory esp-merge-bin APP=matter-lock TARGET="$$t" || exit 1; \
	done
	@mkdir -p '$(ESP_RELEASE_STAGE)'
	@# The browser flasher reads the version out of the manifest. Edited as JSON
	@# rather than by sed: a tag is not pattern-safe text, and in a sed
	@# replacement an & means the whole match, so a tag containing one would
	@# quietly corrupt the file the flasher trusts.
	@python3 -c 'import json,os,sys; m=json.load(open(sys.argv[1])); m["version"]=os.environ["ESP_RELEASE_VER"]; json.dump(m,open(sys.argv[2],"w"),indent=2)' \
	  $(REPO_ROOT)/web-flasher/manifest.json '$(ESP_RELEASE_STAGE)/manifest.json'
	@# The setup code is the same on every board flashed from this release: the
	@# app builds with CHIP's test parameters and no factory-data provider, so
	@# the passcode is a constant. Derived from web-flasher/check_codes.py rather
	@# than written out here, because that file is gated against upstream drift
	@# and a second copy of the number would not be.
	@bins=; for t in $(ESP_RELEASE_CHIPS); do \
	    bins="$$bins $(ALIRO_BUILD_ROOT)/esp32-matter-lock-$$t/openaliro-matter-lock-$$t.bin"; \
	  done; \
	  code=$$(cd $(REPO_ROOT)/web-flasher && python3 -c 'import check_codes; print(check_codes.manual_code())' 2>/dev/null); \
	  $(REPO_ROOT)/scripts/release-bundle.sh \
	    --target esp32-matter-lock --out '$(ESP_RELEASE_OUT)' \
	    --version "$$ESP_RELEASE_VER" \
	    --board 'ESP32-S3, ESP32-C5 or ESP32-C6 + DWM3000EVB' \
	    $${code:+--setup-code "$$code"} \
	    --commission-note 'Type this into Apple Home. The same code, and a QR to scan instead, print on the serial console at 115200 baud.' \
	    $$bins '$(ESP_RELEASE_STAGE)/manifest.json'

## esp-env: sanity-check the toolchain — prints idf.py's version and the paths
esp-env: esp-check-env
	@cd "$(ESP_APP_DIR)" && $(IDFPY) --version
	@printf '  APP             = %s\n  TARGET          = %s\n  VARIANT         = %s\n  BUILD           = %s\n  IDF_EXPORT      = %s\n  ESP_MATTER_PATH = %s\n' \
	  '$(APP)' '$(TARGET)' '$(if $(VARIANT),$(VARIANT),(none))' '$(ESP_BUILD)' '$(IDF_EXPORT)' '$(ESP_MATTER_PATH)'

## esp-term: serial console via tio  ·  no ESP backtrace decode, but robust
##   tio holds the port, so quit it (ctrl-t q) before make esp-flash.
##   Override: make esp-term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
esp-term:
	@command -v tio >/dev/null 2>&1 || { printf '  tio not found  ·  install: brew install tio\n' >&2; exit 1; }
	@$(RESOLVE_PORT); \
	logargs=; [ -n '$(LOG)' ] && logargs='-L --log-file $(LOG)'; \
	printf '  tio %s @ %s 8N1  ·  ctrl-t q quits  ·  release before make esp-flash\n' "$$port" '$(BAUD)'; \
	exec tio -b $(BAUD) $$logargs "$$port"

## esp-ports: list attached usbmodem ports, labelled by board
esp-ports:
	@ioreg -l -w0 2>/dev/null | LC_ALL=C awk \
	  '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} \
	   /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c); \
	     lbl="unknown ("vend")"; \
	     if(vend=="$(VID_WCH)")lbl="ESP  (WCH UART)  <- flash target"; \
	     else if(vend=="$(VID_ESPRESSIF)")lbl="ESP  (native USB) <- flash target"; \
	     else if(vend=="$(VID_SEGGER)")lbl="nRF J-Link  ** DO NOT FLASH **"; \
	     printf "  %-34s %s\n", c, lbl}' ; \
	ls /dev/cu.usbmodem* >/dev/null 2>&1 || echo "  (no usbmodem ports attached)"

## esp-clean: remove THIS app/target/variant's build directory
##   `make clean` at the root removes every build there is, this one only.
esp-clean:
	@rm -rf "$(ESP_BUILD)" && printf '  removed %s\n' '$(ESP_BUILD)'

##@ ESP32 presence  (CONFIG_WOZ_PRESENCE · adds `presence` to the app's console)
## esp-presence-on: add the presence console command to this build  ·  idempotent
##   Additive, not a mode: the shell, the logs and ranging all stay exactly as
##   they are, and `presence` joins them. On the reader this also enables
##   CONFIG_WOZ_ALIRO_CLONE, whose aliro-export prints the reader PRIVATE KEY on
##   the console -- bench only, which is why it stays default n.
##   Writes to this build's own sdkconfig, so it cannot leak into another target.
esp-presence-on:
	@[ -f "$(ESP_SDKCONFIG)" ] || { echo "  no sdkconfig yet — run 'make esp-build APP=$(APP)' once first" >&2; exit 1; }
	@for k in CONFIG_WOZ_PRESENCE $(if $(filter reader,$(APP)),CONFIG_WOZ_ALIRO_CLONE); do \
		grep -qx "$$k=y" "$(ESP_SDKCONFIG)" || printf '%s=y\n' "$$k" >> "$(ESP_SDKCONFIG)"; \
	done; echo "  presence ON  ·  next: make esp-presence-flash APP=$(APP)"

## esp-presence-off: drop the presence commands from this build
esp-presence-off:
	@[ -f "$(ESP_SDKCONFIG)" ] || { echo "  no sdkconfig yet — nothing to turn off"; exit 0; }
	@sed -i '' -e '/^CONFIG_WOZ_PRESENCE=y$$/d' -e '/^CONFIG_WOZ_ALIRO_CLONE=y$$/d' \
	  "$(ESP_SDKCONFIG)" && echo "  presence OFF  ·  next: make esp-flash APP=$(APP)"

## esp-presence-flash: turn presence on, then build + flash
##   Never erases: the device signing key and the Aliro trust store both live in
##   NVS, and erasing regenerates the key, invalidating every enrolment.
esp-presence-flash: esp-presence-on
	@$(MAKE) --no-print-directory esp-flash

## esp-presence-clone: copy a provisioned reader identity onto the dongle  ·  SRC=/dev/cu...
##   The phone's Wallet credential was issued against ONE reader identity, so the
##   dongle has to present that same identity rather than be enrolled separately.
##   SRC is the provisioned board (it needs aliro-export, i.e. its own presence-on).
esp-presence-clone:
	@[ -n "$(SRC)" ] || { echo "  set SRC=  ·  the provisioned board's port (see: make esp-ports)" >&2; exit 1; }
	@$(RESOLVE_PORT); \
	[ "$$port" != "$(SRC)" ] || { echo "  SRC and the dongle resolved to the same port ($$port)" >&2; \
	  echo "  pass PORT= as well to say which is the dongle" >&2; exit 1; }; \
	echo "  cloning $(SRC) -> $$port"; \
	cd $(REPO_ROOT) && python3 $(PRESENCE) clone --source "$(SRC)" --port "$$port"

## esp-presence-probe: bring-up check  ·  does the board sign a frame we accept?
##   Starts a fresh Aliro authentication + UWB round and succeeds only when the
##   pinned credential proves present inside the configured distance.
esp-presence-probe:
	@$(RESOLVE_PORT); \
	echo "  probing $$port"; \
	python3 $(PRESENCE) probe --port "$$port" --max-cm $(MAXCM)

## esp-presence-enroll: record this board's public key as trusted  ·  NAME=desk-lock
##   Appends to .presence/enrolled at the repo root, which is committed so the
##   trust set is reviewable in history.
esp-presence-enroll:
	@[ -n "$(NAME)" ] || { echo "  set NAME=  ·  e.g. make esp-presence-enroll NAME=desk-lock" >&2; exit 1; }
	@$(RESOLVE_PORT); \
	cd $(REPO_ROOT) && python3 $(PRESENCE) enroll --port "$$port" --name "$(NAME)"

## esp-presence-sign: create a presence-signed annotated tag  ·  TAG=presence/1.2.0
##   Wake the phone and hold it near the board first; this refuses to tag
##   unless presence actually verifies.
esp-presence-sign:
	@[ -n "$(TAG)" ] || { echo "  set TAG=  ·  e.g. make esp-presence-sign TAG=presence/1.2.0" >&2; exit 1; }
	@$(RESOLVE_PORT); \
	cd $(REPO_ROOT) && python3 $(PRESENCE) sign --tag "$(TAG)" --port "$$port" --max-cm $(MAXCM)

# ---- matter-lock only --------------------------------------------------------
# Recovery build with normal PIV PIN enforcement, and the walk-up profiler.
# Both are reached through VARIANT=piv / the lab target rather than a second
# build directory of their own.
.PHONY: esp-piv-pin-build esp-piv-pin-app-flash

## esp-piv-pin-build: isolated recovery build with normal PIV PIN enforcement
esp-piv-pin-build:
	@$(MAKE) --no-print-directory esp-build APP=matter-lock VARIANT=piv

## esp-piv-pin-app-flash: restore the PIN-enforced PIV app without erasing NVS
esp-piv-pin-app-flash:
	@$(MAKE) --no-print-directory esp-app-flash APP=matter-lock VARIANT=piv

## esp-lab: capture a walk-up to a timestamped log, then auto-score it
##   The trace ships in every build, OFF at boot. At the matter> console:
##   `lab on`, walk up + unlock, `lab off`, then ctrl-t q.
esp-lab:
	@command -v tio >/dev/null 2>&1 || { printf '  tio not found · install: brew install tio\n' >&2; exit 1; }
	@command -v python3 >/dev/null 2>&1 || { printf '  python3 not found\n' >&2; exit 1; }
	@$(RESOLVE_PORT); \
	log="walkup-$$(date +%Y%m%d-%H%M%S).log"; \
	printf '  %s @ %s · at the console: `lab on`, walk up, `lab off`, then ctrl-t q\n' "$$port" '$(BAUD)'; \
	tio -b $(BAUD) -L --log-file "$$log" "$$port" || true; \
	printf '\n  scoring %s\n' "$$log"; \
	python3 $(REPO_ROOT)/tools/aliro_lab.py "$$log" "$$log.html" || true; \
	{ command -v open >/dev/null 2>&1 && [ -f "$$log.html" ] && open "$$log.html"; } || true

# Help shown by the app-directory forwarders, so `cd apps/reader && make` still
# prints something useful. Lists this file's ESP rows only.
esp-help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sOpenAliro ESP32%s  %s·  APP=%s TARGET=%s · run from the repo root as make esp-<target>%s\n' \
	  "$$b" "$$r" "$$d" '$(APP)' '$(TARGET)' "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ ESP32/ { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## esp-/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-22s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(REPO_ROOT)/mk/esp32.mk; \
	printf '\n'
