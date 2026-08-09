/* matterfake esp_matter_ota.h — declared only; encrypted OTA is compiled out. */
#ifndef MATTERFAKE_ESP_MATTER_OTA_H
#define MATTERFAKE_ESP_MATTER_OTA_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t esp_matter_ota_requestor_encrypted_init(const char *key, uint16_t key_len);

#endif /* MATTERFAKE_ESP_MATTER_OTA_H */
