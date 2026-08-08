# SPDX-License-Identifier: Apache-2.0

if(CONFIG_DW3000)

zephyr_library()

# OS-neutral glue only; the Zephyr dw3000_hw/dw3000_spi backends live in
# ports/zephyr/dw3000/ (module woz_zephyr_port), the ESP-IDF ones in
# ports/esp32/components/woz_uwb/port/. platform/ keeps the contract headers.
# The source set is roles/core.list + roles/chip_dw3000.list — the same lists
# ports/esp32/components/woz_uwb reads, so the two ports cannot drift.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/woz_roles.cmake)
woz_role_sources(${CMAKE_CURRENT_SOURCE_DIR}/roles/core.list DW_CORE_SRCS)
woz_role_sources(${CMAKE_CURRENT_SOURCE_DIR}/roles/chip_dw3000.list DW_CHIP_SRCS)

zephyr_library_sources(${DW_CORE_SRCS})

zephyr_library_sources_ifdef(CONFIG_DW3000_CHIP_DW3000 ${DW_CHIP_SRCS})
zephyr_library_sources_ifdef(CONFIG_DW3000_CHIP_DW3720 dwt_uwb_driver/dw3720/dw3720_device.c)

zephyr_include_directories(platform)
zephyr_include_directories(dwt_uwb_driver)
zephyr_include_directories(dwt_uwb_driver/lib/qmath/include)

endif(CONFIG_DW3000)
