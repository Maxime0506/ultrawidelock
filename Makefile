# Makefile — single command entry point for the Aliro NFC + UWB firmware.
#
# Thin front door over scripts/bootstrap.sh and scripts/build.sh (which hold the real logic:
# preflight, pristine-vs-incremental signature detection, chip resolution), plus
# the host-side test/coverage targets (plain C, no NCS toolchain or hardware).
#
#   make                 # this grouped, colourised help
#   make build           # incremental build   -> build/merged.hex
#   make test            # host KAT test for the pure CCC core
#   make coverage        # line coverage of all our code (+ HTML report)
#   make build PRETTY=1 CHIP=dw3720   # build options (build targets only)

.DEFAULT_GOAL := help

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Options forwarded to build.sh. Set on the command line: make build PRETTY=1.
# The in-tree Aliro stack is the default; ALIRO_SOURCE=0 selects the legacy
# Nordic archive for comparison or regression isolation.
CHIP     ?=
PRETTY   ?=
PRISTINE ?=
SELFTEST ?=
STRICT   ?=
# HA=1 opts into the Home Assistant variant. It must be set on BOTH bootstrap
# (applies the data-model patches) and build (layers woz-ha.conf); see
# integration/homeassistant/README.md. Not hardware-validated.
HA            ?=
ALIRO_SOURCE  ?=
ALIRO_TRACE   ?=
NFC           ?=
CIR           ?=

# Serial monitor (make term). PORT auto-detects the nRF5340DK console (VCOM1 —
# this firmware's console + Zephyr shell live there; VCOM0 is silent). Override
# PORT/BAUD if auto-detect picks the wrong board; set LOG=file.log to also tee
# the raw stream to a file.
PORT     ?=
BAUD     ?= 115200
LOG      ?=

# Which security gates `make security` runs. Empty means all eight.
GATES    ?=

# Presence-signed tags (make presence-verify TAG=presence/1.2.0). MAXCM is the
# distance threshold in cm; 40 matches tools/presence_verify.py's default.
TAG      ?=
MAXCM    ?= 40
PRESENCE_RUNTIME_OUT ?= $(REPO_ROOT)/build/presence-runtime.tar.gz

# Assemble the env prefix from whichever options were set.
ENV := $(strip \
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

.PHONY: help tools tools-install bootstrap ws-seed ws-clean build rebuild pretty selftest test test-san ha-stage0 ha-test ha-package ha-setup check coverage test-port test-ws test-web presence-runtime presence-verify docs docs-publish fuzz cbmc verify security security-web security-ct security-workspace security-fw security-attest flash flash-erase term openaliro tui tui-setup tui-test tui-release clean

##@ Setup
## tools: what every host CI gate needs, what this machine has, how to fill gaps
##   Reports each tool, the gate it serves, and the version CI pins where it
##   pins one. Installs nothing. Exits nonzero when something is missing, so
##   `make verify` skipping a gate is never a surprise.
tools:
	@$(REPO_ROOT)/scripts/toolchain.sh check

## tools-install: install the missing host tools  ·  prints the commands, asks first
##   macOS/Linux, via whichever of brew/apt/dnf/pacman/zypper is present, plus
##   pipx for the version-pinned python tools. Nothing runs before you agree;
##   `-y` or ASSUME_YES=1 answers in advance. Also fetches nrfutil, which is
##   reported but never required: it belongs to `make build`, not to any gate,
##   so its absence never fails this target. The firmware toolchains themselves
##   are separate: `make bootstrap` (NCS) and ESP-IDF, see docs/set-up.md.
tools-install:
	@$(REPO_ROOT)/scripts/toolchain.sh install

## bootstrap: set this machine up for the repo  ·  the only command before build
##   Three phases, so a fresh clone reaches `make build` without a manual step
##   in the middle: `make tools-install` for the host gate tools and nrfutil;
##   the NCS v3.3.0 toolchain (~2 GB, skipped when already installed); then NCS
##   + the Nordic add-on (~6.5 GB), patched for this repo. Anything it cannot
##   install stops the run before the big fetch, not after it.
##   CI never runs this target — it calls scripts/bootstrap.sh directly — so no
##   runner has its packages touched.
##   Options: NO_TOOLS=1 skip the tool phase, straight to the fetch
##            NO_TOOLCHAIN=1 skip the NCS toolchain phase
##            HA=1 also applies the Home Assistant data-model patches
##            (pair with `make build HA=1`; not hardware-validated)
bootstrap:
	@[ -n "$(NO_TOOLS)" ] || $(REPO_ROOT)/scripts/toolchain.sh install
	@$(ENV) ./scripts/bootstrap.sh

## ws-seed: give THIS worktree its own workspace (APFS COW clone, ~0 disk)
##   Idempotent. Isolates worktrees so branch-bouncing can't build stale patches.
ws-seed:
	@$(REPO_ROOT)/scripts/ws-seed.sh

##@ Build
## build: incremental build            -> build/merged.hex
##   Options: CHIP=dw3720 (default dw3000)  PRETTY=1  PRISTINE=1  SELFTEST=1
##            STRICT=1 (drop suspect ranges)
##            HA=1 (Home Assistant variant — needs `make bootstrap HA=1` too)
##            ALIRO_SOURCE=0 (legacy Nordic binary fallback)
##            ALIRO_TRACE=1 (currently blocked: vendor trace patch is absent)
##            CIR=1 (CIA/CIR diagnostics: `aliro cir on|dump on|probe`)
##            NFC=pn532|st25r|none (reader transport; default st25r)
##   e.g.     make build PRETTY=1 CHIP=dw3720
build:
	@$(ENV) ./scripts/build.sh build

## rebuild: force clean pristine build
rebuild:
	@$(ENV) ./scripts/build.sh rebuild

## selftest: one-shot boot self-test (no iPhone)
selftest:
	@$(ENV) UWB_SELFTEST=1 ./scripts/build.sh build

## pretty: build with curated / quiet console
pretty:
	@$(ENV) PRETTY=1 ./scripts/build.sh build

##@ Test
## tui-test: run the OpenTUI source tests (no hardware required)
tui-test:
	@cd $(REPO_ROOT)/tools/tui && bun run test

## test: run the host test suite for our logic  (no NCS toolchain / hardware)
test:
	@$(REPO_ROOT)/tests/host/run.sh

## coverage: line coverage of every host suite, 0% rows for the rest  ->  table + HTML
##   Instrumented (clang source-based coverage); slower than `make test` and
##   rebuilt at -O0. Artifacts under build/coverage/ (html/index.html).
coverage:
	@$(REPO_ROOT)/tests/host/coverage.sh

## test-san: host suite rebuilt under ASan + UBSan  ·  memory-bug gate
test-san:
	@SAN=1 $(REPO_ROOT)/tests/host/run.sh

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

## fuzz: fuzz the wire-facing parsers  ·  parser-hardening gate
##   Coverage-guided libFuzzer where available (CI), else a portable corpus
##   replay under ASan/UBSan (Apple clang ships no libFuzzer). Env: FUZZ_SECONDS=N,
##   FUZZ_STANDALONE=1. Args (via bash) restrict to named targets.
fuzz:
	@$(REPO_ROOT)/tests/host/fuzz.sh

## cbmc: bounded model-check the wire parsers  ·  memory-safety proof
##   Proves no out-of-bounds / bad-pointer access for all inputs up to each
##   harness bound. Needs cbmc on PATH (brew install cbmc / apt install cbmc).
cbmc:
	@$(REPO_ROOT)/tests/host/cbmc.sh

## verify: run every host-runnable CI gate in one shot  ·  pre-push sweep
##   The 22 CI jobs a host can run — format, shellcheck, clang-tidy, fuzz, test,
##   twin-wasm, patch-drift, docs, test-san, test-port, test-ws, test-verify,
##   coverage (with the 90% floor), zizmor, licences, the four security gates,
##   cbmc — run in parallel lanes behind a serial tripwire, so a 1s formatting
##   slip stops it at once. A gate whose tool is missing
##   FAILS the sweep (`make tools-install` fixes it), because CI runs that gate
##   regardless. cbmc is the exception: 64s on its own, twice the rest of the
##   sweep, so it is off unless WITH_CBMC=1 (~72s), and its row says so.
##   The semgrep gate fetches its registry rule packs, so the sweep now wants
##   network; SEMGREP_NO_REGISTRY=1 drops to the in-tree rules only.
##   Builds no firmware, not even in a shell with ESP-IDF sourced (test-port's
##   target-build layer is held off, as it is on CI's runner). firmware-builds
##   and release stay out: ESP-IDF + NCS, tens of minutes.
##   Options: WITH_CBMC=1 adds the proof  ·  SKIP="fuzz docs" drops named gates
##            SERIAL=1 one gate at a time  ·  COV_MIN=90 coverage floor
verify:
	@$(REPO_ROOT)/scripts/verify.sh

## security: the eight blocking security gates  ·  what a PR must pass
##   secrets (gitleaks) · mal-diff (structural review of this branch's diff) ·
##   semgrep (SAST, ERROR blocks and WARNING reports) · deps (osv-scanner +
##   pip-audit, which also covers known-MALICIOUS packages via OSV's MAL- feed) ·
##   web (CDN pins, SRI, CSP, retire.js, install flags) · ct (secret-dependent branches) ·
##   esp (component registry pins) · attest (release provenance).
##   ~30s. Identical to what .github/workflows/security.yml runs, because both
##   call scripts/security.sh. Name one to run it alone:
##     make security GATES="semgrep deps"
##   ct reports "not checked" on macOS: there is no valgrind for darwin/arm64,
##   so it exits 2 rather than passing. CT_DOCKER=1 runs it in a container.
##   Slower analyses (full-history secrets, semgrep SARIF, Scorecard) are not
##   here: they run weekly in security-deep.yml and block nothing. CodeQL runs
##   under GitHub's default setup, configured in the repository settings.
security:
	@$(REPO_ROOT)/scripts/security.sh $(GATES)

## security-web: browser supply chain  ·  CDN pins, SRI, CSP, retire.js, install flags
##   Covers web-flasher/, web-twin/, release/*/FLASH.html and the docs theme.
##   Known debt lives in security/web-baseline.txt; an entry there that stops
##   matching FAILS the gate, so a line cannot outlive its problem.
security-web:
	@$(REPO_ROOT)/scripts/security-web.sh

## security-ct: constant-time  ·  secret-dependent branches in the CCC ladder
##   ctgrind under valgrind: the URSK is poisoned, so a branch on undefined data
##   IS a branch on the key. No valgrind on darwin/arm64 — the gate says so and
##   exits 2 rather than passing. CT_DOCKER=1 runs it in a container.
security-ct:
	@$(REPO_ROOT)/scripts/security-ct.sh

## security-workspace: the fetched dependencies  ·  west pins, ESP components, SBOM
##   `esp` needs nothing; `pins sbom vulns` need `make bootstrap` first and run
##   in the deep CI lane. WS_UPDATE_PINS=1 records the resolved pin set.
security-workspace:
	@$(REPO_ROOT)/scripts/security-workspace.sh $(GATES)

## security-fw: the shipped artifact  ·  key material, build-host paths, size
##   Runs on build/merged.hex, so it needs `make build` first.
##   FW_UPDATE_BASELINE=1 records the current size as the baseline.
security-fw:
	@$(REPO_ROOT)/scripts/security-fw.sh

## security-attest: release provenance is configured  ·  and `verify <tag>` proves it works
security-attest:
	@$(REPO_ROOT)/scripts/security-attest.sh workflow

## presence-runtime: build the eight-file macOS transfer archive
##   Output: build/presence-runtime.tar.gz  ·  override with PRESENCE_RUNTIME_OUT=
presence-runtime:
	@python3 $(REPO_ROOT)/scripts/presence_runtime.py --output "$(PRESENCE_RUNTIME_OUT)"

## presence-verify: check a tag's presence assertion  ·  TAG=presence/1.2.0  (what CI runs)
##   Confirms a human was physically at the machine when the tag was made. Pure
##   host check — no dongle, no serial port. Trusted keys come from
##   .presence/enrolled, never from the tag. Building and signing live in
##   ports/esp32/apps/reader (make presence-probe / presence-sign).
presence-verify:
	@[ -n "$(TAG)" ] || { echo "  set TAG=  ·  e.g. make presence-verify TAG=presence/1.2.0" >&2; exit 1; }
	@python3 $(REPO_ROOT)/tools/presence_git.py verify --tag "$(TAG)" --max-cm $(MAXCM)

## check: every host-side suite under one banner  ->  live rows + summary table
##   Parallel by default; SERIAL=1 streams suites one at a time, SUITES="..."
##   scopes (firmware shared webtwin). The pre-push look, runnable any time.
check:
	@$(REPO_ROOT)/scripts/test-runner.sh

## test-port: host-runnable ESP32 port tests (port headers, crypto KATs, codec)
##   No ESP-IDF needed; the on-target build check inside skips cleanly without it.
test-port:
	@$(REPO_ROOT)/ports/esp32/test/run.sh

## test-ws: hermetic tests for per-worktree workspace auto-seeding
##   Runs in a temp dir with a stub bootstrap — no west, no hardware, and it
##   never touches this repo's own workspace/ or build/.
test-ws:
	@$(REPO_ROOT)/tests/tooling/ws_seed_test.sh

## test-verify: tests for the gates themselves  ·  make verify's own gate
##   Two files. verify_test.sh has two halves — static: the gate table still
##   covers every job in .github/workflows/, so a new CI job cannot be added
##   without either a local gate or a written reason; behavioral: a copy of
##   verify.sh run against stub tools in a temp git repo, checking that a missing
##   tool, a failed tripwire and an unmet coverage floor each fail the sweep
##   rather than passing quietly. security_diff_test.sh plants a binary, a mode
##   change, a symlink and a gitlink in a throwaway repo and checks the
##   malicious-change gate blocks each one — none of which can be asserted by
##   reading the script.
##   Nothing real is compiled; both files run in a couple of seconds.
test-verify:
	@$(REPO_ROOT)/tests/tooling/verify_test.sh
	@$(REPO_ROOT)/tests/tooling/security_diff_test.sh

## test-web: drift-gate the web-twin page against the firmware it cites
##   Re-reads every constant web-twin/index.html cites (file:line) from the C
##   tree and fails if a value moved. The decision logic itself is the real
##   firmware compiled to WASM (twin.js) — this guards the residual JS-side
##   constants (the ESP32 walk-up controller port + world pacing).
test-web:
	@python3 $(REPO_ROOT)/web-twin/check_constants.py

## twin-wasm: compile the twin's firmware to WASM  ->  web-twin/twin.js
##   modules/woz_uwb + the tests/host shim under Emscripten (needs emsdk on
##   PATH or in ~/emsdk). Reproducible: the committed twin.js is rebuilt and
##   byte-diffed by CI, so the page can never run stale firmware.
twin-wasm:
	@$(REPO_ROOT)/scripts/twin-wasm.sh

## test-twin: rebuild the WASM twin, then replay the test_twin.c scenario in node
test-twin: twin-wasm
	@node $(REPO_ROOT)/web-twin/selftest.cjs

##@ Docs
## docs: build the documentation site  ->  site/index.html
##   Subsystem tree + guides + search, then the Doxygen reference under site/api/,
##   then a link pass that fails the build on any dead link. Needs doxygen and
##   graphviz; no NCS toolchain or hardware.
docs:
	@$(REPO_ROOT)/scripts/docs.sh

## docs-publish: rebuild the site, then snapshot it onto the local gh-pages branch
##   Never pushes: publishing stays `git push origin gh-pages`. Refuses a stale
##   or partial site, uncommitted docs/, or a foreign branch named gh-pages.
docs-publish: docs
	@$(REPO_ROOT)/scripts/docs-publish.sh

##@ Flash
## flash: app-only flash
flash:
	@$(ENV) ./scripts/build.sh flash

## flash-erase: full erase + flash  ·  needed after a net-core change
flash-erase:
	@$(ENV) ./scripts/build.sh flash-erase

##@ Monitor
## tui-setup: install pinned OpenTUI dependencies and build its local bundle
tui-setup:
	@command -v bun >/dev/null 2>&1 || { printf '  Bun 1.3+ is required  ·  https://bun.sh\n' >&2; exit 1; }
	@cd $(REPO_ROOT)/tools/tui && bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*' && bun run build

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

## tui-release: build reproducible macOS arm64 + Linux x64 TUI executables
tui-release:
	@cd $(REPO_ROOT)/tools/tui && bun install --frozen-lockfile --ignore-scripts --os='*' --cpu='*' && bun run release

## term: interactive serial console — live logs + typeable shell (tio, 115200 8N1)
##   Auto-detects the nRF5340DK console (VCOM1).  ctrl-t q quits.  Type `help` for shell commands.
##   Override: make term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
term:
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

##@ Housekeeping
## clean: remove ./build
clean:
	@rm -rf build && printf '  removed ./build\n'

## ws-clean: remove THIS worktree's local build + seeded workspace
##   Frees the per-worktree caches; re-seed with `make ws-seed`. A symlinked
##   workspace (pointing at the primary) is left alone — only a real local dir
##   is removed, never the shared source.
ws-clean:
	@rm -rf build
	@if [ -d workspace ] && [ ! -L workspace ]; then rm -rf workspace && printf '  removed ./build + ./workspace\n'; \
	else printf '  removed ./build (no local workspace to remove)\n'; fi

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sAliro NFC + UWB firmware%s  %s·  nrf5340dk/nrf5340/cpuapp%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-16s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n  %sOptions%s  %s·  set on the command line, e.g. make build PRETTY=1%s\n' "$$y" "$$r" "$$d" "$$r"; \
	printf '    %sCHIP=dw3720  PRETTY=1  PRISTINE=1  SELFTEST=1  STRICT=1%s\n' "$$d" "$$r"; \
	printf '    %sHA=1  ·  Home Assistant variant; set on bootstrap AND build%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_SOURCE=0  ·  legacy Nordic binary fallback%s\n' "$$d" "$$r"; \
	printf '    %sCIR=1  ·  CIA/CIR diagnostics%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_TRACE=1  ·  unavailable: required vendor trace patch is absent%s\n' "$$d" "$$r"; \
	printf '    %sNFC=pn532|st25r|none  ·  reader transport; default st25r%s\n' "$$d" "$$r"; \
	printf '\n'
