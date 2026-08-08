# Device-side (initiator) BLE transport as an ESP-IDF component — the central
# mirror of the aliro_ble component. aliro_ble_central_nimble.c is the NimBLE
# backend and stays ESP32-only; the decoders it calls are platform-free and
# come from the ble_central role manifest (modules/woz_aliro/roles/) so the
# host suite and a future Zephyr backend share exactly one copy.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/ble_central.list" ALIRO_BLE_CENTRAL_SRCS)

idf_component_register(
	SRCS "aliro_ble_central_nimble.c" ${ALIRO_BLE_CENTRAL_SRCS}
	INCLUDE_DIRS "${ALIRO}/include"
	REQUIRES bt nvs_flash
)
