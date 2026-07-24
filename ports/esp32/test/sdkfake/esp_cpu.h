/* sdkfake esp_cpu.h — monotonic fake cycle counter. */
#ifndef SDKFAKE_ESP_CPU_H
#define SDKFAKE_ESP_CPU_H

#include "sdkfake.h"

extern uint32_t fake_cpu_cycles; /* test-settable; bumps by 1 per read */

static inline uint32_t esp_cpu_get_cycle_count(void)
{
	return ++fake_cpu_cycles;
}

#endif
