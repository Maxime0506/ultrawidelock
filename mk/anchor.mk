# mk/anchor.mk — the two-anchor DS-TWR bench link.
#
# One application, two roles, two boards. Neither target touches the lock: this
# is a separate app directory with its own build directories, so nothing here
# can change what a DWM3001CDK boots as a lock.
#
# Reuses mk/cdk.mk's CDK_WEST / CDK_RUN / CDK_PROBE machinery rather than
# restating it. That is deliberate for CDK_PROBE in particular: stage A puts two
# debug probes on one machine, which is exactly the case that makefile measured
# enumerating in a different order twenty minutes apart.

ANCHOR_APP   := $(REPO_ROOT)/anchor
ANCHOR_BOARD ?= decawave_dwm3001cdk

# ROLE=initiator|responder. Defaults to initiator because the door node is the
# one that sets the cadence and a bare `make anchor-build` should give the half
# that drives the link.
ROLE ?= initiator
ifeq ($(filter $(ROLE),initiator responder),)
$(error ROLE must be initiator or responder, got '$(ROLE)')
endif
ANCHOR_ROLE_FLAG := $(if $(filter $(ROLE),initiator),\
                      -DCONFIG_ANCHOR_ROLE_INITIATOR=y,\
                      -DCONFIG_ANCHOR_ROLE_RESPONDER=y)

# One build directory per (board, role). Four combinations exist and mixing two
# of them in one directory is how you flash an initiator believing it is a
# responder -- which presents as a dead link, not as an error.
#
# The board string is flattened first: `nrf5340dk/nrf5340/cpuapp` carries slashes
# and would otherwise bury the build three directories deep, where the ELF paths
# the flash and monitor targets print stop being copy-pasteable.
ANCHOR_BOARD_TAG := $(subst /,_,$(ANCHOR_BOARD))
ANCHOR_BUILD ?= $(ALIRO_BUILD_ROOT)/anchor-$(ANCHOR_BOARD_TAG)-$(ROLE)

# The chip follows the board, and this is not cosmetic: `probe-rs attach` needs
# the right target or it fails in a way that reads like a dead board. CDK_CHIP
# from mk/cdk.mk is the nRF52833 and is only correct for that half of the pair.
# Names verified against `probe-rs chip list` (0.32.0), not recalled.
ifeq ($(ANCHOR_BOARD),decawave_dwm3001cdk)
ANCHOR_CHIP := nRF52833_xxAA
else
ANCHOR_CHIP := nRF5340_xxAA_APPONLY
endif
override ANCHOR_BUILD := $(abspath $(ANCHOR_BUILD))

# ANT_DLY: the lumped per-PAIR antenna-delay constant, in DTU. Empty leaves the
# Kconfig default of 0, which means uncalibrated. See anchor/Kconfig for why it
# is one number rather than two registers, and the plan for how to solve it.
ANCHOR_ANT_FLAG := $(if $(ANT_DLY),-DCONFIG_ANCHOR_ANT_DLY_DTU=$(ANT_DLY))

ANCHOR_PRISTINE := $(if $(PRISTINE),always,auto)

.PHONY: anchor-build anchor-flash anchor-monitor anchor-pair

##@ Two-anchor bench  ·  anchor-to-anchor DS-TWR (stage A)
## anchor-build: build one anchor  -> build/anchor-<board>-<role>
anchor-build:
	@$(CDK_RUN) build -p $(ANCHOR_PRISTINE) -b $(ANCHOR_BOARD) \
	  -d $(ANCHOR_BUILD) $(ANCHOR_APP) \
	  -- $(ANCHOR_ROLE_FLAG) $(ANCHOR_ANT_FLAG)

## anchor-flash: flash the anchor built by the same ROLE/ANCHOR_BOARD pair
anchor-flash:
	$(CDK_PROBE_GUARD)
	@$(CDK_RUN) flash -d $(ANCHOR_BUILD) $(CDK_DEV_ID_ARG)

## anchor-monitor: RTT console for one anchor  ·  attaches with the ELF in its build dir
anchor-monitor:
	$(CDK_PROBE_GUARD)
	@probe-rs attach --chip $(ANCHOR_CHIP) $(CDK_PROBE_ARG) \
	  $(ANCHOR_BUILD)/anchor/zephyr/zephyr.elf

## anchor-pair: build BOTH halves of today's pair in one go
anchor-pair:
	@$(MAKE) anchor-build ROLE=responder ANCHOR_BOARD=decawave_dwm3001cdk
	@$(MAKE) anchor-build ROLE=initiator ANCHOR_BOARD=nrf5340dk/nrf5340/cpuapp
