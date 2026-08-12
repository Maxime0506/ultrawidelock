# credential BLE transport as an ESP-IDF component.
#
# The NimBLE backend is shared: modules/ultrawidelock_cred/src/ultrawidelock_ble_nimble.c names no OS and
# builds on any NimBLE host, listed in roles/ble_transport.list. ultrawidelock_ble_esp32.c is the
# ESP-IDF half of bring-up (NVS, nimble_port_init, the host task). The standalone FreeRTOS
# port pairs the same backend with its own bring-up file.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CRED "${REPO_ROOT}/modules/ultrawidelock_cred")

include("${REPO_ROOT}/cmake/ultrawidelock_roles.cmake")
ultrawidelock_role_sources("${CRED}/roles/ble_transport.list" ULTRAWIDELOCK_BLE_TRANSPORT_SRCS)

idf_component_register(
	SRCS "ultrawidelock_ble_esp32.c" ${ULTRAWIDELOCK_BLE_TRANSPORT_SRCS}
	INCLUDE_DIRS "${CRED}/include"
	REQUIRES bt nvs_flash
	# ultrawidelock_advtag_derive (the Dynamic Tag) lives in the shared crypto component.
	PRIV_REQUIRES ultrawidelock_crypto
)

# The shared backend logs through modules/ultrawidelock_port's contract, not esp_log directly.
target_include_directories(${COMPONENT_LIB} PRIVATE "${REPO_ROOT}/modules/ultrawidelock_port/include")
