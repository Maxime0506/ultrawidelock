# Aliro BLE transport as an ESP-IDF component.
#
# The NimBLE backend is shared: modules/woz_aliro/src/aliro_ble_nimble.c names no OS and
# builds on any NimBLE host, listed in roles/ble_transport.list. aliro_ble_esp32.c is the
# ESP-IDF half of bring-up (NVS, nimble_port_init, the host task). The standalone FreeRTOS
# port pairs the same backend with its own bring-up file.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/ble_transport.list" ALIRO_BLE_TRANSPORT_SRCS)

idf_component_register(
	SRCS "aliro_ble_esp32.c" ${ALIRO_BLE_TRANSPORT_SRCS}
	INCLUDE_DIRS "${ALIRO}/include"
	REQUIRES bt nvs_flash
	# aliro_advtag_derive (the Dynamic Tag) lives in the shared crypto component.
	PRIV_REQUIRES aliro_crypto
)

# The shared backend logs through modules/woz_port's contract, not esp_log directly.
target_include_directories(${COMPONENT_LIB} PRIVATE "${REPO_ROOT}/modules/woz_port/include")
