/* matterfake led_strip_rmt.h — RMT backend config + constructor double. */
#ifndef MATTERFAKE_LED_STRIP_RMT_H
#define MATTERFAKE_LED_STRIP_RMT_H

#include "led_strip.h"

#define RMT_CLK_SRC_DEFAULT 0

typedef struct {
	int clk_src;
	uint32_t resolution_hz;
	uint32_t mem_block_symbols;
	struct {
		uint32_t with_dma : 1;
	} flags;
} led_strip_rmt_config_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
				   const led_strip_rmt_config_t *rmt_config,
				   led_strip_handle_t *ret_strip);

#endif /* MATTERFAKE_LED_STRIP_RMT_H */
