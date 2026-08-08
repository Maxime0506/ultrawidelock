# The device self-test + the shared credential-auth core, built with the real
# PSA backend. ALIRO_DEVICE_HAVE_EC turns on the full ECDH+ECDSA loopback.
# The test body is port-neutral (tests/shared) and the sources are the same
# role manifests the nRF twin of this app reads
# (ports/nrf5340dk/on_target_ec/CMakeLists.txt).
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")
set(SHARED "${REPO_ROOT}/tests/shared")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/device.list" ALIRO_DEVICE_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ALIRO_APDU_SRCS)
woz_role_sources("${ALIRO}/roles/crypto.list" ALIRO_CRYPTO_SRCS)
woz_role_sources("${ALIRO}/roles/hash.list" ALIRO_HASH_SRCS)

idf_component_register(
	SRCS
		"app_main.c"
		"${SHARED}/test_aliro_device.c"
		${ALIRO_DEVICE_SRCS}
		${ALIRO_APDU_SRCS}
		${ALIRO_CRYPTO_SRCS}
		${ALIRO_HASH_SRCS}
	INCLUDE_DIRS "."
	PRIV_INCLUDE_DIRS "${ALIRO}/include" "${ALIRO}/src"
	REQUIRES mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ALIRO_DEVICE_HAVE_EC)
