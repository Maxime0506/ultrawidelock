# nRF52840 BLE RSSI witness (inside / outside / threshold).
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

list(APPEND ZEPHYR_EXTRA_MODULES
	"${REPO_ROOT}/ports/zephyr")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ultrawidelock_ble_witness)

target_sources(app PRIVATE src/main.c)
