set(PIV_SRCS "piv_ccid.c" "piv_apdu.c")

if(CONFIG_WOZ_PIV_CCID)
	list(APPEND PIV_SRCS "piv_ccid_usb.c" "piv_identity.c")
endif()

# Component requirements are expanded before project Kconfig values are loaded,
# so these cannot be conditional on CONFIG_WOZ_PIV_CCID.
idf_component_register(
	SRCS ${PIV_SRCS}
	INCLUDE_DIRS "include"
	PRIV_REQUIRES
		aliro_crypto
		aliro_reader
		espressif__esp_tinyusb
		espressif__tinyusb
		mbedtls
		nvs_flash
)
