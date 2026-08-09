# mk/witness.mk — nRF52840 BLE RSSI witnesses (inside/outside/threshold).
#
# Uses WITNESS_ROLE (not ROLE) so it does not collide with mk/anchor.mk.

WITNESS_APP   := $(REPO_ROOT)/examples/zephyr/ble-witness
WITNESS_BOARD ?= nrf52840dk/nrf52840
WITNESS_ROLE ?= inside

ifeq ($(filter $(WITNESS_ROLE),inside outside threshold),)
$(error WITNESS_ROLE must be inside, outside, or threshold; got '$(WITNESS_ROLE)')
endif

WITNESS_ROLE_FLAG := $(if $(filter $(WITNESS_ROLE),inside),-DCONFIG_WITNESS_ROLE_INSIDE=y,\
	$(if $(filter $(WITNESS_ROLE),outside),-DCONFIG_WITNESS_ROLE_OUTSIDE=y,\
	-DCONFIG_WITNESS_ROLE_THRESHOLD=y))

WITNESS_BOARD_TAG := $(subst /,_,$(WITNESS_BOARD))
WITNESS_BUILD ?= $(ALIRO_BUILD_ROOT)/witness-$(WITNESS_BOARD_TAG)-$(WITNESS_ROLE)
override WITNESS_BUILD := $(abspath $(WITNESS_BUILD))
WITNESS_PRISTINE := $(if $(PRISTINE),always,auto)

.PHONY: witness-build witness-flash witness-trio

##@ BLE witnesses  ·  inside / outside / threshold RSSI
## witness-build: build one BLE witness  -> build/witness-<board>-<role>
witness-build:
	@$(CDK_RUN) build -p $(WITNESS_PRISTINE) -b $(WITNESS_BOARD) \
	  -d $(WITNESS_BUILD) $(WITNESS_APP) \
	  -- $(WITNESS_ROLE_FLAG)

## witness-flash: flash the witness built by the same WITNESS_ROLE/BOARD pair
witness-flash:
	@$(CDK_RUN) flash -d $(WITNESS_BUILD)

## witness-trio: build inside + outside + threshold for WITNESS_BOARD
witness-trio:
	@$(MAKE) witness-build WITNESS_ROLE=inside WITNESS_BOARD=$(WITNESS_BOARD)
	@$(MAKE) witness-build WITNESS_ROLE=outside WITNESS_BOARD=$(WITNESS_BOARD)
	@$(MAKE) witness-build WITNESS_ROLE=threshold WITNESS_BOARD=$(WITNESS_BOARD)
