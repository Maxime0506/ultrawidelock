# Anchor-to-anchor DS-TWR bench app.
#
# Deliberately NOT a sysbuild application with a bootloader. apps/dwm3001cdk-lock/ carries
# MCUboot and therefore needs `make dfu-key` before it will configure; this is a
# bench tool that gets flashed over SWD and never updated over the air, so
# giving it a signing key would add a setup step and buy nothing.
#
# Only two modules: ultrawidelock_uwb for the CONFIG_ULTRAWIDELOCK_UWB tier and modules/ultrawidelock_dw3000 under
# it. ultrawidelock_cred and ultrawidelock_matter are absent because nothing here is Aliro-aware --
# that is the point of building at this tier.
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

list(APPEND ZEPHYR_EXTRA_MODULES
	"${REPO_ROOT}/modules/ultrawidelock_uwb"
	"${REPO_ROOT}/modules/ultrawidelock_dw3000"
	"${REPO_ROOT}/ports/zephyr")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ultrawidelock_anchor)

# Same guard apps/dwm3001cdk-lock/CMakeLists.txt carries, for the same reason: recovering
# debug access on either board costs a mass erase, and on the CDK that takes
# settings_storage and the reader identity with it. A bench image is exactly the
# kind of thing that would inherit the setting from a stray overlay unnoticed.
if(CONFIG_NRF_APPROTECT_LOCK OR CONFIG_NRF_SECURE_APPROTECT_LOCK)
	message(FATAL_ERROR
		"APPROTECT lock is enabled for this image. Refusing to build.\n"
		"  CONFIG_NRF_APPROTECT_LOCK        = ${CONFIG_NRF_APPROTECT_LOCK}\n"
		"  CONFIG_NRF_SECURE_APPROTECT_LOCK = ${CONFIG_NRF_SECURE_APPROTECT_LOCK}\n"
		"See apps/dwm3001cdk-lock/sysbuild.conf for why these boards never lock.")
endif()

# Two files, and nothing borrowed from the CCC tree. An earlier version of this
# compiled modules/ultrawidelock_uwb/src/ccc/ccc_mac.c in to reuse ccc_ds_twr_tof(); that
# helper accumulates its numerator unsigned and returns garbage below about a
# metre, so anchor_twr.c computes the same formula signed instead. See the
# comment at that computation for why this is the tree's own precedent rather
# than a shortcut.
target_sources(app PRIVATE
	src/main.c
	src/anchor_twr.c)
