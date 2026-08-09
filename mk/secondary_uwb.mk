# mk/secondary_uwb.mk — nRF5340 + DWM3000EVB secondary-UWB experiment scaffold.

SEC_UWB_APP   := $(REPO_ROOT)/examples/zephyr/secondary-uwb
SEC_UWB_BOARD ?= nrf5340dk/nrf5340/cpuapp
SEC_UWB_BUILD ?= $(ALIRO_BUILD_ROOT)/secondary-uwb
override SEC_UWB_BUILD := $(abspath $(SEC_UWB_BUILD))
SEC_UWB_PRISTINE := $(if $(PRISTINE),always,auto)

.PHONY: secondary-uwb-build

##@ Secondary UWB experiment  ·  nRF5340 + DWM3000EVB
## secondary-uwb-build: scaffold image (observe-only; no unlock authority)
secondary-uwb-build:
	@$(CDK_RUN) build -p $(SEC_UWB_PRISTINE) -b $(SEC_UWB_BOARD) \
	  -d $(SEC_UWB_BUILD) $(SEC_UWB_APP)
