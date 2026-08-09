# Experimental secondary UWB observer (nRF5340 DK + DWM3000EVB).
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

list(APPEND ZEPHYR_EXTRA_MODULES
	"${REPO_ROOT}/modules/woz_uwb"
	"${REPO_ROOT}/modules/woz_dw3000"
	"${REPO_ROOT}/ports/zephyr")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(openaliro_secondary_uwb)

target_sources(app PRIVATE src/main.c)
