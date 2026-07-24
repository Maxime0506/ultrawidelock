# The device self-test + the shared credential-auth core, built with the real
# PSA backend. ALIRO_DEVICE_HAVE_EC turns on the full ECDH+ECDSA loopback.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")
set(TEST "${REPO_ROOT}/ports/esp32/test")

idf_component_register(
	SRCS
		"app_main.c"
		"${TEST}/test_aliro_device.c"
		"${ALIRO}/src/aliro_device.c"
		"${ALIRO}/src/aliro_device_apdu.c"
		"${ALIRO}/src/aliro_apdu.c"
		"${ALIRO}/src/aliro_crypto.c"
		"${ALIRO}/src/aliro_hash.c"
		"${ALIRO}/src/aliro_prim_psa.c"
	INCLUDE_DIRS "."
	PRIV_INCLUDE_DIRS "${ALIRO}/include" "${ALIRO}/src"
	REQUIRES mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ALIRO_DEVICE_HAVE_EC)
