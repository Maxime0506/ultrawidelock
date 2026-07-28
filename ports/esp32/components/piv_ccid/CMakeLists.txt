set(PIV_SRCS "piv_ccid.c" "piv_apdu.c")

if(CONFIG_WOZ_PIV_CCID)
	list(APPEND PIV_SRCS "piv_ccid_usb.c" "piv_identity.c")
endif()

# Component requirements are expanded before project Kconfig values are loaded,
# so these cannot be conditional on CONFIG_WOZ_PIV_CCID. IDF_TARGET is set by
# then, though, and the Kconfig option depends on IDF_TARGET_ESP32S3 anyway, so
# the USB-OTG components are required only where the peripheral exists. The
# manifest rule scopes the download the same way: esp32c5 has USB Serial/JTAG
# but no OTG, so naming these unconditionally fails to resolve there.
if(IDF_TARGET STREQUAL "esp32s3")
	set(PIV_USB_REQUIRES espressif__esp_tinyusb espressif__tinyusb)
else()
	set(PIV_USB_REQUIRES "")
endif()

idf_component_register(
	SRCS ${PIV_SRCS}
	INCLUDE_DIRS "include"
	PRIV_REQUIRES
		aliro_crypto
		aliro_reader
		${PIV_USB_REQUIRES}
		mbedtls
		nvs_flash
)
