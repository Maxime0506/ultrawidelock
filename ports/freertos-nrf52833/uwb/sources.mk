# The UWB engine's source set and configuration for this port, as a make
# fragment the target build graph includes.
#
# The source set is the role manifests in modules/{woz_uwb,woz_dw3000}/roles/ --
# the same lists the Zephyr module and the ESP-IDF component read -- plus this
# directory's two platform backends. Nothing is listed here that a manifest
# already lists: a source assigned to a role belongs in the manifest and nowhere
# else, which is what keeps the three ports from drifting apart.
#
# Include with REPO_ROOT set:
#   include $(REPO_ROOT)/ports/freertos-nrf52833/uwb/sources.mk
# and consume WOZ_UWB_SRCS, WOZ_UWB_INCLUDES and WOZ_UWB_DEFINES.

WOZ_UWB_ROLES := $(REPO_ROOT)/modules/woz_uwb/roles
WOZ_DW_ROLES  := $(REPO_ROOT)/modules/woz_dw3000/roles

# Strip comments and blanks, then make each path absolute. Same contract as
# cmake/woz_roles.cmake, so the two build systems cannot disagree about a role.
# The hash is escaped because make would otherwise take it as a comment and
# swallow the rest of the line, closing parenthesis included.
woz_role_sources = $(addprefix $(REPO_ROOT)/,$(strip \
	$(shell sed -e 's/\#.*$$//' $(1) | tr -d '\r')))

# The manifests are named once, here, rather than inline in the expansion
# below. A manifest that is renamed or deleted expands to nothing at all -- the
# role's sources simply vanish and the link fails somewhere else entirely -- so
# the list has to be visible to a check that can assert each file exists.
WOZ_UWB_ROLE_LISTS := \
	$(WOZ_UWB_ROLES)/base_driver.list \
	$(WOZ_UWB_ROLES)/base_engine.list \
	$(WOZ_UWB_ROLES)/responder_driver.list \
	$(WOZ_UWB_ROLES)/responder_engine.list \
	$(WOZ_UWB_ROLES)/ccc_keys.list \
	$(WOZ_UWB_ROLES)/ccc_engine.list \
	$(WOZ_UWB_ROLES)/crypto_psa.list \
	$(WOZ_UWB_ROLES)/aliro_adapter.list \
	$(WOZ_UWB_ROLES)/aliro_codec.list \
	$(WOZ_DW_ROLES)/core.list \
	$(WOZ_DW_ROLES)/chip_dw3000.list

WOZ_UWB_SRCS := \
	$(foreach list,$(WOZ_UWB_ROLE_LISTS),$(call woz_role_sources,$(list))) \
	$(REPO_ROOT)/ports/freertos-nrf52833/uwb/dw3000_spi_freertos.c \
	$(REPO_ROOT)/ports/freertos-nrf52833/uwb/dw3000_hw_freertos.c

WOZ_UWB_INCLUDES := \
	$(REPO_ROOT)/modules/woz_port/include \
	$(REPO_ROOT)/modules/woz_uwb/include \
	$(REPO_ROOT)/modules/woz_uwb/src/driver \
	$(REPO_ROOT)/modules/woz_uwb/src/fira \
	$(REPO_ROOT)/modules/woz_uwb/src/ccc \
	$(REPO_ROOT)/modules/woz_uwb/src/facade \
	$(REPO_ROOT)/modules/woz_uwb/src/aliro \
	$(REPO_ROOT)/modules/woz_dw3000/include \
	$(REPO_ROOT)/modules/woz_dw3000/dwt_uwb_driver \
	$(REPO_ROOT)/modules/woz_dw3000/dwt_uwb_driver/lib/qmath/include \
	$(REPO_ROOT)/ports/freertos-nrf52833/uwb

WOZ_UWB_DEFINES := \
	CONFIG_WOZ_UWB=1 \
	CONFIG_WOZ_UWB_RESPONDER=1 \
	CONFIG_WOZ_ALIRO=1 \
	CONFIG_DW3000=1 \
	CONFIG_DW3000_CHIP_DW3000=1 \
	CONFIG_WOZ_CRYPTO_PSA=1 \
	CONFIG_WOZ_UWB_FINAL_SNAPSHOT=1

# CONFIG_WOZ_CRYPTO_PSA, not the mbedTLS variant the ESP-IDF port selects. The
# CCC STS key derivation reaches AES-ECB through a seam behind ccc_kdf.h, and
# this port has a PSA provider where that one did not: crypto/ builds Mbed TLS
# standalone with the PSA core on, for aliro_prim_psa.c's sake. Selecting the
# mbedTLS variant here would link a second, lower-level path to the same
# primitive for no reason.
#
# CONFIG_WOZ_UWB_FINAL_SNAPSHOT is not optional on this part and is not a
# diagnostic. This is a single core, and BLE shares it with the ranging
# callbacks: without the snapshot the live DS-TWR timestamps are overwritten
# before Final_Data is processed, and the ranges come out kilometres wide. The
# nRF5340 oracle does not need it because its network core is not doing this.
