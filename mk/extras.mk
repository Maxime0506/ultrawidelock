# mk/extras.mk — the things that hang off the firmware rather than build it:
# the Home Assistant integration, presence-signed tags, the guided bench TUI,
# and housekeeping. Included last, so `make help` ends here.

.PHONY: ha-stage0 ha-test ha-package ha-setup \
        presence-runtime presence-verify \
        openaliro tui tui-setup tui-test tui-release \
        clean ws-clean help

##@ Home Assistant  (HA=1 only — never part of the default build or test path)
## ha-stage0: validate the HA=1-only Stage 0 evidence and fixture contract
##   No firmware, serial device, or broker is opened. Requires an explicit
##   `HA=1` so this productization work cannot enter the default test path.
ha-stage0:
	@[ "$(HA)" = "1" ] || { printf '%s\n' 'ha-stage0 requires HA=1'; exit 2; }
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_stage0.py

## ha-test: run HA=1-only Home Assistant host tests
##   Starts with Stage 0 evidence and the shared parser. Requires `HA=1`; it
##   neither builds firmware nor opens a serial device or MQTT connection.
ha-test:
	@[ "$(HA)" = "1" ] || { printf '%s\n' 'ha-test requires HA=1'; exit 2; }
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_stage0.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_parser.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_config.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_mqtt.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_cli.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_compatibility.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_serial_session.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_serial_transport.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_agent.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_setup.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_component.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_package.py
	@HA=1 python3 -B $(REPO_ROOT)/tests/host/test_ha_firmware_contract.py

## ha-package: build a local custom-component beta archive
##   Requires HA=1 and never publishes. The component archive vendors the shared
##   library so it can be installed as one local custom-component archive.
ha-package:
	@[ "$(HA)" = "1" ] || { printf '%s\n' 'ha-package requires HA=1'; exit 2; }
	@python3 -B $(REPO_ROOT)/integration/homeassistant/tools/package_component.py

## ha-setup: set up the broker TLS, agent config, and credentials in one step
##   Requires HA=1. Talks to a real broker over SSH and writes into
##   ~/.config/openaliro-ha, so it is never part of a test or build path.
##   Override defaults with HA_SSH, BROKER_HOST, MQTT_USER, or DEVICE_ID.
ha-setup:
	@[ "$(HA)" = "1" ] || { printf '%s\n' 'ha-setup requires HA=1'; exit 2; }
	@$(REPO_ROOT)/integration/homeassistant/scripts/ha-setup.sh

##@ Presence  (a human was physically at the machine when the tag was made)
## presence-runtime: build the eight-file macOS transfer archive
##   Output: build/presence-runtime.tar.gz  ·  override with PRESENCE_RUNTIME_OUT=
presence-runtime:
	@python3 $(REPO_ROOT)/scripts/presence_runtime.py --output "$(PRESENCE_RUNTIME_OUT)"

## presence-verify: check a tag's presence assertion  ·  TAG=presence/1.2.0  (what CI runs)
##   Confirms a human was physically at the machine when the tag was made. Pure
##   host check — no dongle, no serial port. Trusted keys come from
##   .presence/enrolled, never from the tag. Building and signing live on the
##   board: make esp-presence-probe / esp-presence-sign.
presence-verify:
	@[ -n "$(TAG)" ] || { echo "  set TAG=  ·  e.g. make presence-verify TAG=presence/1.2.0" >&2; exit 1; }
	@python3 $(REPO_ROOT)/tools/presence_git.py verify --tag "$(TAG)" --max-cm $(MAXCM)

##@ Bench
## openaliro: start the guided bench  ·  installs its pinned TUI dependencies on first run
openaliro:
	@command -v bun >/dev/null 2>&1 || { \
	  printf '\n  The guided OpenAliro bench needs Bun 1.3 or newer.\n'; \
	  printf '  Install Bun, then run this exact command again:  make openaliro\n'; \
	  printf '  Installation guide: https://bun.sh\n\n'; exit 1; }
	@cd $(REPO_ROOT)/tools/tui && { \
	  [ -d node_modules ] || { printf '  First run: preparing the guided bench…\n'; bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*'; }; \
	  bun run dev; }

## tui: compatibility alias for `make openaliro`
tui: openaliro

## tui-setup: install pinned OpenTUI dependencies and build its local bundle
tui-setup:
	@command -v bun >/dev/null 2>&1 || { printf '  Bun 1.3+ is required  ·  https://bun.sh\n' >&2; exit 1; }
	@cd $(REPO_ROOT)/tools/tui && bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*' && bun run build

## tui-test: run the OpenTUI source tests (no hardware required)
tui-test:
	@cd $(REPO_ROOT)/tools/tui && bun run test

## tui-release: build reproducible macOS arm64 + Linux x64 TUI executables
tui-release:
	@cd $(REPO_ROOT)/tools/tui && bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*' && bun run release

##@ Housekeeping
## clean: remove every build artifact in the tree  ->  ./build and the app-local ones
##   One rm for the shared root, plus the directories ESP-IDF writes when idf.py
##   is called directly inside an app instead of through `make esp-build`.
clean:
	@# ALIRO_BUILD_ROOT is `?=` and exported (Makefile:38-39), so whatever is in
	@# the caller's environment wins -- and this line deletes it recursively. A
	@# stale export from another checkout would aim that delete outside the repo,
	@# so refuse anything that is not a real subdirectory of it. `..` is rejected
	@# separately because a path can start with $(REPO_ROOT) and still climb out.
	@root='$(ALIRO_BUILD_ROOT)'; repo='$(REPO_ROOT)'; \
	case "$$root" in \
	  *..*) printf '  refusing: ALIRO_BUILD_ROOT contains ".." -- %s\n' "$$root" >&2; exit 1;; \
	  "$$repo"/?*) ;; \
	  *) printf '  refusing: ALIRO_BUILD_ROOT is not inside %s -- %s\n' "$$repo" "$$root" >&2; \
	     printf '  It is exported, so a value left in your environment redirects this delete.\n' >&2; \
	     exit 1;; \
	esac; \
	rm -rf "$$root"
	@# The variable is quoted but the globs are not, which is the point: quoting
	@# the whole word would stop `*` expanding.
	@rm -rf "$(REPO_ROOT)"/ports/esp32/apps/*/build "$(REPO_ROOT)"/ports/esp32/apps/*/build-piv \
	        "$(REPO_ROOT)"/ports/esp32/test/on_target_ec/build "$(REPO_ROOT)"/ports/nrf5340dk/on_target_ec/build
	@# The TUI gate compiles into its own directory rather than the build root
	@# (bun decides where its output goes), so it needs naming here or `clean`
	@# leaves it behind. node_modules is a fetched dependency, not output: kept.
	@rm -rf "$(REPO_ROOT)"/tools/tui/dist "$(REPO_ROOT)"/tools/tui/.*.bun-build
	@printf '  removed %s, the app-local build directories and the TUI bundle\n' '$(ALIRO_BUILD_ROOT)'

## ws-clean: remove THIS worktree's local build + seeded workspace
##   Frees the per-worktree caches; re-seed with `make ws-seed`. A symlinked
##   workspace (pointing at the primary) is left alone — only a real local dir
##   is removed, never the shared source.
ws-clean: clean
	@if [ -d workspace ] && [ ! -L workspace ]; then rm -rf workspace && printf '  removed ./workspace\n'; \
	else printf '  (no local workspace to remove)\n'; fi

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sOpenAliro%s  %s·  Aliro NFC + UWB firmware  ·  bare targets mean the DWM3001CDK%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-18s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n  %sOptions%s  %s·  set on the command line, e.g. make nrf-build PRETTY=1%s\n' "$$y" "$$r" "$$d" "$$r"; \
	printf '    %sPRISTINE=1  ·  from-scratch build (every port)%s\n' "$$d" "$$r"; \
	printf '    %sLTO=0  RELEASE=1  SMP=1  DFU_LOG=1  ·  DWM3001CDK%s\n' "$$d" "$$r"; \
	printf '    %sCDK_BUILD=<dir>  CDK_RTT_BUILD=<dir>  CDK_KEY=<path>  ·  DWM3001CDK%s\n' "$$d" "$$r"; \
	printf '    %sAPP=matter-lock|reader|initiator  TARGET=esp32s3|esp32c5|esp32c6  VARIANT=presence|hamqtt|piv%s\n' "$$d" "$$r"; \
	printf '    %sCHIP=dw3720  PRETTY=1  SELFTEST=1  STRICT=1  ·  nRF5340 DK%s\n' "$$d" "$$r"; \
	printf '    %sHA=1  ·  Home Assistant variant; set on bootstrap AND nrf-build%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_SOURCE=0  ·  legacy Nordic binary fallback -> build/nrf5340dk-blob%s\n' "$$d" "$$r"; \
	printf '    %sCIR=1  ·  CIA/CIR diagnostics%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_TRACE=1  ·  unavailable: required vendor trace patch is absent%s\n' "$$d" "$$r"; \
	printf '    %sNFC=pn532|st25r|none  ·  reader transport; default st25r%s\n' "$$d" "$$r"; \
	printf '\n  %sMoved%s  %scdk-aliro-matter-thread -> build   cdk-reader -> reader   cdk-rtt -> monitor   term -> nrf-term%s\n' "$$y" "$$r" "$$d" "$$r"; \
	printf '\n'
