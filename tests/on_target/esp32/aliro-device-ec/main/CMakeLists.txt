# The device self-test + the shared credential-auth core, built with the real
# PSA backend. ULTRAWIDELOCK_DEVICE_HAVE_EC turns on the full ECDH+ECDSA loopback.
# The test body is port-neutral (tests/shared) and the sources are the same
# role manifests the nRF twin of this app reads
# (tests/on_target/zephyr/nrf5340dk-aliro-device-ec/CMakeLists.txt).
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/ultrawidelock_cred")
set(SHARED "${REPO_ROOT}/tests/shared")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/device.list" ULTRAWIDELOCK_DEVICE_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ULTRAWIDELOCK_APDU_SRCS)
woz_role_sources("${ALIRO}/roles/crypto.list" ULTRAWIDELOCK_CRYPTO_SRCS)
woz_role_sources("${ALIRO}/roles/hash.list" ULTRAWIDELOCK_HASH_SRCS)

idf_component_register(
	SRCS
		"app_main.c"
		"${SHARED}/test_aliro_device.c"
		${ULTRAWIDELOCK_DEVICE_SRCS}
		${ULTRAWIDELOCK_APDU_SRCS}
		${ULTRAWIDELOCK_CRYPTO_SRCS}
		${ULTRAWIDELOCK_HASH_SRCS}
	INCLUDE_DIRS "."
	PRIV_INCLUDE_DIRS "${ALIRO}/include" "${ALIRO}/src"
	REQUIRES mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ULTRAWIDELOCK_DEVICE_HAVE_EC)
