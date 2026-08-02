# mk/setup.mk — getting a machine ready: host gate tools, then the NCS toolchain
# and the fetched west workspace both Zephyr ports build against.

.PHONY: tools tools-install bootstrap ws-seed

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
##   reported but never required: it belongs to the firmware builds, not to any
##   gate, so its absence never fails this target. The firmware toolchains
##   themselves are separate: `make bootstrap` (NCS) and ESP-IDF, see docs/set-up.md.
tools-install:
	@$(REPO_ROOT)/scripts/toolchain.sh install

## bootstrap: set this machine up for the repo  ·  the only command before build
##   Three phases, so a fresh clone reaches `make build` without a manual step
##   in the middle: `make tools-install` for the host gate tools and nrfutil;
##   the NCS v3.3.0 toolchain (~2 GB, skipped when already installed); then NCS
##   + the Nordic add-on (~6.5 GB), patched for this repo. Anything it cannot
##   install stops the run before the big fetch, not after it.
##   Both Zephyr ports build out of this one workspace: the DWM3001CDK needs it
##   for Zephyr and the NCS toolchain, the nRF5340 DK also for the add-on.
##   CI never runs this target — it calls scripts/bootstrap.sh directly — so no
##   runner has its packages touched.
##   In a linked worktree that has no ./workspace yet, this delegates to ws-seed:
##   a COW clone of the primary's tree costs ~0 disk, where refetching costs 6.5 GB.
##   Delegation is skipped once ./workspace exists, because ws-seed is a no-op then
##   and bootstrap's real job is re-applying THIS branch's patches.
##   Options: NO_TOOLS=1 skip the tool phase, straight to the fetch
##            NO_TOOLCHAIN=1 skip the NCS toolchain phase
##            NO_SEED=1 in a worktree, fetch a full independent workspace anyway
##            (for a different NCS revision, or a primary you suspect is corrupt)
##            HA=1 also applies the Home Assistant data-model patches
##            (pair with `make nrf-build HA=1`; not hardware-validated)
bootstrap:
	@[ -n "$(NO_TOOLS)" ] || $(REPO_ROOT)/scripts/toolchain.sh install
	@if [ -z "$(NO_SEED)" ] && [ ! -d workspace/.west ] && \
	    [ "$$(git rev-parse --git-common-dir)" != "$$(git rev-parse --git-dir)" ]; then \
	  printf '  linked worktree with no workspace: cloning the primary (NO_SEED=1 to refetch)\n'; \
	  $(REPO_ROOT)/scripts/ws-seed.sh && exit 0; \
	  printf '  seeding unavailable; falling back to a full fetch\n'; \
	fi; \
	$(NRF_ENV) ./scripts/bootstrap.sh

## ws-seed: give THIS worktree its own workspace (APFS COW clone, ~0 disk)
##   Idempotent. Isolates worktrees so branch-bouncing can't build stale patches.
ws-seed:
	@$(REPO_ROOT)/scripts/ws-seed.sh
