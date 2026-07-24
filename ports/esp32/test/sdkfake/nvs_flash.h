/* sdkfake nvs_flash.h — init/erase entry points of the in-RAM NVS fake. */
#ifndef SDKFAKE_NVS_FLASH_H
#define SDKFAKE_NVS_FLASH_H

#include "nvs.h"

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#endif
