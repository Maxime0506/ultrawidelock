# Aliro reader engine as an ESP-IDF component.
# The transaction driver, wire codec, trust store and ranging glue come from
# the role manifests in modules/woz_aliro/roles/ — the same lists firmware/
# and the host suite read. Only aliro_prov_nvs.c is ESP32-specific (NVS-backed
# blob store); its Zephyr counterpart will be a settings/nvs twin.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/reader.list" ALIRO_READER_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ALIRO_APDU_SRCS)
woz_role_sources("${ALIRO}/roles/reader_policy.list" ALIRO_POLICY_SRCS)
# Step-up (Access Document) + presence-assertion pure codecs. Always compiled
# (pure functions, no side effects) but only invoked from aliro_reader.c under
# CONFIG_WOZ_ALIRO_STEPUP (default n); aliro_assert.c is backend-free by
# design and only the presence dongle app (CONFIG_WOZ_PRESENCE) calls it.
woz_role_sources("${ALIRO}/roles/wire_codecs.list" ALIRO_WIRE_CODEC_SRCS)

idf_component_register(
	SRCS
		${ALIRO_READER_SRCS}
		${ALIRO_APDU_SRCS}
		${ALIRO_POLICY_SRCS}
		${ALIRO_WIRE_CODEC_SRCS}
		# Single-consumer shared files, deliberately literal (allowlisted as
		# NOT_MANIFESTED in tests/tooling/port_purity_check.sh): the step-up
		# worker's engine half whose ESP worker body is gated to empty when
		# CONFIG_WOZ_ALIRO_STEPUP is off, and the aliro_assert P-256 shim —
		# the only one of the assert pair with a crypto dependency.
		"${ALIRO}/src/aliro_stepup.c"
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
