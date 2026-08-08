# Standalone Zephyr app: run the Aliro device (initiator) self-test on an
# nRF5340 DK against the real nrf_security/CryptoCell PSA P-256 backend
# (aliro_prim_psa.c). Console prints PASS/FAIL. No BLE/UWB/iPhone.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aliro_device_ec_test)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")
# The device self-test body is port-neutral and lives in tests/shared; the
# ESP32 host suite compiles the same file against the fake-EC prim double,
# while this app links it against the real CryptoCell P-256 backend.
set(TESTDIR "${REPO_ROOT}/tests/shared")

# Device-side shared sources by role manifest (modules/woz_aliro/roles/*.list)
# — the same lists the initiator app and the ESP32 components read.
include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/device.list" ALIRO_DEVICE_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ALIRO_APDU_SRCS)
woz_role_sources("${ALIRO}/roles/crypto.list" ALIRO_CRYPTO_SRCS)
woz_role_sources("${ALIRO}/roles/hash.list" ALIRO_HASH_SRCS)

target_sources(app PRIVATE
	src/main.c
	${TESTDIR}/test_aliro_device.c
	${ALIRO_DEVICE_SRCS}
	${ALIRO_APDU_SRCS}
	${ALIRO_CRYPTO_SRCS}
	${ALIRO_HASH_SRCS}
)
target_include_directories(app PRIVATE ${ALIRO}/include ${ALIRO}/src)
target_compile_definitions(app PRIVATE ALIRO_DEVICE_HAVE_EC)
