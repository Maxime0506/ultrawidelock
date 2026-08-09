/* matterfake bsp/esp-bsp.h — one fake board button (matterfake.cc). */
#ifndef MATTERFAKE_BSP_ESP_BSP_H
#define MATTERFAKE_BSP_ESP_BSP_H

#include "esp_err.h"

#define BSP_BUTTON_NUM 1

typedef void *button_handle_t;

esp_err_t bsp_iot_button_create(button_handle_t btn_array[], int *btn_cnt, int btn_array_size);

#endif /* MATTERFAKE_BSP_ESP_BSP_H */
