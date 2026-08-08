# Aliro device/initiator session layer as an ESP-IDF component — the mirror of
# the aliro_reader component. Sources come from the role manifests in
# modules/woz_aliro/roles/ (the same lists ports/nrf5340dk/{initiator,
# on_target_ec} read); nothing here is ESP32-specific.
#
# The apdu role is ALSO compiled by the aliro_reader component, so an app must
# not REQUIRE both this and aliro_reader — it would link the symbol twice.
# Nothing needs to today: a board is either the reader or the initiator, not both.
#
# src/ is a public include dir here, unlike aliro_crypto where it is private:
# the initiator app itself needs aliro_apdu.h for the 4-byte L2CAP envelope
# (aliro_ble_frame/unframe), so it reuses the reader's own framing rather than
# hand-rolling a second copy of the header layout.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/device.list" ALIRO_DEVICE_SRCS)
woz_role_sources("${ALIRO}/roles/apdu.list" ALIRO_APDU_SRCS)

idf_component_register(
	SRCS
		${ALIRO_DEVICE_SRCS}
		${ALIRO_APDU_SRCS}
	INCLUDE_DIRS "${ALIRO}/include" "${ALIRO}/src"
	REQUIRES aliro_crypto
)
