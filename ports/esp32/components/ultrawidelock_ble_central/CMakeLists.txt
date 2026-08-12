# Device-side (initiator) BLE transport as an ESP-IDF component — the central
# mirror of the ultrawidelock_ble component. ultrawidelock_ble_central_nimble.c is the NimBLE
# backend and stays ESP32-only; the decoders it calls are platform-free and
# come from the ble_central role manifest (modules/ultrawidelock_cred/roles/) so the
# host suite and a future Zephyr backend share exactly one copy.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CRED "${REPO_ROOT}/modules/ultrawidelock_cred")

include("${REPO_ROOT}/cmake/ultrawidelock_roles.cmake")
ultrawidelock_role_sources("${CRED}/roles/ble_central.list" ULTRAWIDELOCK_BLE_CENTRAL_SRCS)

idf_component_register(
	SRCS "ultrawidelock_ble_central_nimble.c" ${ULTRAWIDELOCK_BLE_CENTRAL_SRCS}
	INCLUDE_DIRS "${CRED}/include"
	REQUIRES bt nvs_flash
)
