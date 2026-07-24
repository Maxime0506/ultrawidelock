# Aliro reader engine as an ESP-IDF component.
# The transaction driver, wire codec, trust store and ranging glue are the
# shared sources in modules/woz_aliro. Only aliro_prov_nvs.c is ESP32-specific
# (NVS-backed blob store); its Zephyr counterpart will be a settings/nvs twin.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

idf_component_register(
	SRCS
		"${ALIRO}/src/aliro_reader.c"
		"${ALIRO}/src/aliro_apdu.c"
		"${ALIRO}/src/aliro_prov.c"
		"${ALIRO}/src/aliro_ranging.c"
		"${ALIRO}/src/aliro_lat.c"
		"${ALIRO}/src/aliro_approach.c"
		# Step-up (Access Document) phase. The platform-neutral codec + verifier
		# always compile (pure functions, no side effects) but are only invoked
		# from aliro_reader.c under CONFIG_WOZ_ALIRO_STEPUP (default n); the ESP
		# worker's whole body is gated, so it is empty with the option off.
		"${ALIRO}/src/aliro_stepup.c"
		"${ALIRO}/src/aliro_stepup_parse.c"
		"aliro_stepup_worker.c"
		"aliro_prov_nvs.c"
	INCLUDE_DIRS "${ALIRO}/include"
	PRIV_INCLUDE_DIRS "${ALIRO}/src"
	# woz_port.h / woz_log.h (the platform contract aliro_reader.c and
	# aliro_ranging.c are written to) come via woz_uwb's public include dirs
	# (modules/woz_port/include).
	REQUIRES aliro_ble woz_uwb aliro_crypto nvs_flash
)
