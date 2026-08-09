/* matterfake led_strip.h — WS2812 driver as a recording double: rc scriptable
 * (mfk_led_new_rc), pixel writes recorded (mfk_led_r/g/b). */
#ifndef MATTERFAKE_LED_STRIP_H
#define MATTERFAKE_LED_STRIP_H

#include <stdint.h>

#include "esp_err.h"

typedef struct led_strip_t *led_strip_handle_t;

#define LED_PIXEL_FORMAT_GRB 0
#define LED_MODEL_WS2812 0

typedef struct {
	int strip_gpio_num;
	uint32_t max_leds;
	int led_pixel_format;
	int led_model;
	struct {
		uint32_t invert_out : 1;
	} flags;
} led_strip_config_t;

esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t index, uint32_t red,
			      uint32_t green, uint32_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t strip);
esp_err_t led_strip_clear(led_strip_handle_t strip);

#endif /* MATTERFAKE_LED_STRIP_H */
