# Aliro device/initiator session layer as an ESP-IDF component — the mirror of
# the aliro_reader component. Sources come from the role manifests in
# modules/woz_aliro/roles/ (the same lists the Zephyr initiator and on-target
# test read); nothing here is ESP32-specific.
#
# The apdu role is ALSO compiled by the aliro_reader component, so an app must
# not REQUIRE both this and aliro_reader — it would link the symbol twice.
# Nothing needs to today: a board is either the reader or the initiator, not both.
#
# The initiator app uses the public aliro_apdu.h for the 4-byte L2CAP envelope,
# so it reuses the reader's framing rather than hand-rolling a second layout.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/device.list" ALIRO_DEVICE_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ALIRO_APDU_SRCS)

idf_component_register(
	SRCS
		${ALIRO_DEVICE_SRCS}
		${ALIRO_APDU_SRCS}
	INCLUDE_DIRS "${ALIRO}/include"
	PRIV_INCLUDE_DIRS "${ALIRO}/src"
	REQUIRES aliro_crypto
)
