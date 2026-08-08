# SPDX-License-Identifier: Apache-2.0

if(CONFIG_DW3000)

zephyr_library()

# OS-neutral glue only; the Zephyr dw3000_hw/dw3000_spi backends live in
# ports/zephyr/dw3000/ (module woz_zephyr_port), the ESP-IDF ones in
# ports/esp32/components/woz_uwb/port/. platform/ keeps the contract headers.
zephyr_library_sources(
    platform/deca_port.c
    platform/deca_compat.c
    dwt_uwb_driver/deca_interface.c
    dwt_uwb_driver/deca_rsl.c
    dwt_uwb_driver/lib/qmath/src/qmath.c
)

zephyr_library_sources_ifdef(CONFIG_DW3000_CHIP_DW3000 dwt_uwb_driver/dw3000/dw3000_device.c)
zephyr_library_sources_ifdef(CONFIG_DW3000_CHIP_DW3720 dwt_uwb_driver/dw3720/dw3720_device.c)

zephyr_include_directories(platform)
zephyr_include_directories(dwt_uwb_driver)
zephyr_include_directories(dwt_uwb_driver/lib/qmath/include)

endif(CONFIG_DW3000)
