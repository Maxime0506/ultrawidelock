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
		# Presence-assertion codec (non-door primitive): pure functions, always
		# compiled; only the presence dongle app (CONFIG_WOZ_PRESENCE) calls them.
		# aliro_assert.c is backend-free by design; aliro_assert_ec.c is the
		# separate shim that binds its P-256 seam to aliro_prim, and is the only
		# one of the pair with a crypto dependency.
		"${ALIRO}/src/aliro_assert.c"
		"${ALIRO}/src/aliro_assert_ec.c"
		"aliro_stepup_worker.c"
		"aliro_prov_nvs.c"
		# The `presence` console command. Lives here rather than in one app so
		# both the standalone reader and the Matter lock can serve presence: the
		# lock is the board a phone is actually provisioned against, so requiring
		# a separate dongle app would mean cloning an identity to use it at all.
		"presence_link.c"
	INCLUDE_DIRS "${ALIRO}/include" "."
	PRIV_INCLUDE_DIRS "${ALIRO}/src"
	# woz_port.h / woz_log.h (the platform contract aliro_reader.c and
	# aliro_ranging.c are written to) come via woz_uwb's public include dirs
	# (modules/woz_port/include).
	REQUIRES aliro_ble woz_uwb aliro_crypto nvs_flash
)
