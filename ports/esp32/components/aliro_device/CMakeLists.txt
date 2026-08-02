# Aliro device/initiator session layer as an ESP-IDF component — the mirror of
# the aliro_reader component. Sources are the shared ones in modules/woz_aliro;
# nothing here is ESP32-specific. The dependency set is the same one the nRF
# on-target EC test links (ports/nrf5340dk/on_target_ec/CMakeLists.txt), minus
# what the aliro_crypto component already provides.
#
# aliro_apdu.c is ALSO compiled by the aliro_reader component, so an app must not
# REQUIRE both this and aliro_reader — it would link the symbol twice. Nothing
# needs to today: a board is either the reader or the initiator, not both.
#
# src/ is a public include dir here, unlike aliro_crypto where it is private:
# the initiator app itself needs aliro_apdu.h for the 4-byte L2CAP envelope
# (aliro_ble_frame/unframe), so it reuses the reader's own framing rather than
# hand-rolling a second copy of the header layout.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

idf_component_register(
	SRCS
		"${ALIRO}/src/aliro_device.c"
		"${ALIRO}/src/aliro_device_apdu.c"
		"${ALIRO}/src/aliro_apdu.c"
	INCLUDE_DIRS "${ALIRO}/include" "${ALIRO}/src"
	REQUIRES aliro_crypto
)
