/* sdkfake esp_app_desc.h */
#ifndef SDKFAKE_ESP_APP_DESC_H
#define SDKFAKE_ESP_APP_DESC_H

#include "sdkfake.h"

typedef struct {
	char project_name[32];
	char version[32];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);

#endif
