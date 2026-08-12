/*
 * OpenThread's settings contract, reproduced for the host build. Key values
 * are upstream's, which scripts/freertos-radio-source-check.sh checks.
 */
#ifndef TEST_OPENTHREAD_SETTINGS_H
#define TEST_OPENTHREAD_SETTINGS_H

#include <stdint.h>

#include <openthread/error.h>
#include <openthread/tasklet.h>

enum {
	OT_SETTINGS_KEY_ACTIVE_DATASET = 0x0001,
	OT_SETTINGS_KEY_PENDING_DATASET = 0x0002,
	OT_SETTINGS_KEY_NETWORK_INFO = 0x0003,
	OT_SETTINGS_KEY_PARENT_INFO = 0x0004,
	OT_SETTINGS_KEY_CHILD_INFO = 0x0005,
	OT_SETTINGS_KEY_SRP_ECDSA_KEY = 0x000b,
	OT_SETTINGS_KEY_VENDOR_RESERVED_MIN = 0x8000,
};

void otPlatSettingsInit(otInstance *instance, const uint16_t *sensitive_keys,
			uint16_t sensitive_keys_length);
void otPlatSettingsDeinit(otInstance *instance);
otError otPlatSettingsGet(otInstance *instance, uint16_t key, int index, uint8_t *value,
			  uint16_t *value_length);
otError otPlatSettingsSet(otInstance *instance, uint16_t key, const uint8_t *value,
			  uint16_t value_length);
otError otPlatSettingsAdd(otInstance *instance, uint16_t key, const uint8_t *value,
			  uint16_t value_length);
otError otPlatSettingsDelete(otInstance *instance, uint16_t key, int index);
void otPlatSettingsWipe(otInstance *instance);

#endif /* TEST_OPENTHREAD_SETTINGS_H */
