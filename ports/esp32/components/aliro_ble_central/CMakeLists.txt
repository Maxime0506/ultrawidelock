# Device-side (initiator) BLE transport as an ESP-IDF component — the central
# mirror of the aliro_ble component. aliro_ble_central_nimble.c is the NimBLE
# backend and stays ESP32-only; the decoders it calls are platform-free and are
# compiled from modules/woz_aliro so the host suite and a future Zephyr backend
# share exactly one copy.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

idf_component_register(
	SRCS "aliro_ble_central_nimble.c" "${ALIRO}/src/aliro_ble_central.c"
	INCLUDE_DIRS "${ALIRO}/include"
	REQUIRES bt nvs_flash
)
