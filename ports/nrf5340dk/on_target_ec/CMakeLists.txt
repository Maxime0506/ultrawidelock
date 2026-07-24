# Standalone Zephyr app: run the Aliro device (initiator) self-test on an
# nRF5340 DK against the real nrf_security/CryptoCell PSA P-256 backend
# (aliro_prim_psa.c). Console prints PASS/FAIL. No BLE/UWB/iPhone.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aliro_device_ec_test)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")
set(TESTDIR "${REPO_ROOT}/ports/esp32/test")

target_sources(app PRIVATE
	src/main.c
	${TESTDIR}/test_aliro_device.c
	${ALIRO}/src/aliro_device.c
	${ALIRO}/src/aliro_device_apdu.c
	${ALIRO}/src/aliro_apdu.c
	${ALIRO}/src/aliro_crypto.c
	${ALIRO}/src/aliro_hash.c
	${ALIRO}/src/aliro_prim_psa.c
)
target_include_directories(app PRIVATE ${ALIRO}/include ${ALIRO}/src)
target_compile_definitions(app PRIVATE ALIRO_DEVICE_HAVE_EC)
