#include "advertising_core.h"

#include <string.h>

void woz_aliro_dynamic_tag_input(const uint8_t address_le[WOZ_ALIRO_BLE_ADDRESS_SIZE],
				 uint32_t expiry_timestamp,
				 uint8_t plaintext[WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE])
{
	memset(plaintext, 0, 6);
	for (unsigned int i = 0; i < WOZ_ALIRO_BLE_ADDRESS_SIZE; ++i) {
		plaintext[6 + i] = address_le[WOZ_ALIRO_BLE_ADDRESS_SIZE - 1 - i];
	}
	plaintext[12] = (uint8_t)(expiry_timestamp >> 24);
	plaintext[13] = (uint8_t)(expiry_timestamp >> 16);
	plaintext[14] = (uint8_t)(expiry_timestamp >> 8);
	plaintext[15] = (uint8_t)expiry_timestamp;
}

void woz_aliro_dynamic_tag_from_ciphertext(
	const uint8_t ciphertext[WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE],
	uint8_t tag[WOZ_ALIRO_DYNAMIC_TAG_SIZE])
{
	memcpy(tag, ciphertext, WOZ_ALIRO_DYNAMIC_TAG_SIZE);
}
