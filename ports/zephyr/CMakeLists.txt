# ports/zephyr — woz_zephyr_port: the Zephyr platform backends, one module.
# Mirror of ports/esp32/components/*, which implements the same contracts on
# ESP-IDF; modules/ stays the single portable source tree. Contract headers
# stay with their modules (dw3000_hw.h / dw3000_spi.h in
# modules/woz_dw3000/platform) — this tree carries only Zephyr .c backends.
# Contributes nothing unless a consuming CONFIG is y: an image that selects no
# feature below gets zero bytes from this module.

if(CONFIG_DW3000)

zephyr_library_named(woz_zephyr_port)

# Zephyr GPIO/SPI/trace backend of the dw3000_hw.h + dw3000_spi.h contract
# (ESP-IDF backend: ports/esp32/components/woz_uwb/port/dw3000_{hw,spi}.c).
zephyr_library_sources(
	dw3000/dw3000_hw.c
	dw3000/dw3000_spi.c
	dw3000/dw3000_spi_trace.c)

# Contract headers + decadriver API; modules/woz_dw3000 also publishes these
# via zephyr_include_directories, listed here so this library does not depend
# on that module's processing order.
zephyr_library_include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../modules/woz_dw3000/platform
	${CMAKE_CURRENT_SOURCE_DIR}/../../modules/woz_dw3000/dwt_uwb_driver)

endif()
