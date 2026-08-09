/* sdkfake esp_rom_sys.h — busy-wait delays are recorded, never slept. */
#ifndef SDKFAKE_ESP_ROM_SYS_H
#define SDKFAKE_ESP_ROM_SYS_H

#include "sdkfake.h"

extern uint32_t fake_rom_delay_us_total;
extern int fake_rom_delay_calls;

static inline void esp_rom_delay_us(uint32_t us)
{
	fake_rom_delay_us_total += us;
	fake_rom_delay_calls++;
}

#endif
