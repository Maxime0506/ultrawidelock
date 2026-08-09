#include "woz_freertos_platform.h"

#include "FreeRTOS.h"

#include <platform/nrf_802154_random.h>
#include <platform/nrf_802154_temperature.h>

static uint32_t s_random_state;

void nrf_802154_random_init(void)
{
	int rc;

	do {
		rc = woz_freertos_entropy(&s_random_state, sizeof(s_random_state));
		if (rc != 0) {
			woz_freertos_fatal("nRF 802.15.4 entropy initialization failed");
		}
	} while (s_random_state == 0u);
}

void nrf_802154_random_deinit(void)
{
	s_random_state = 0u;
}

uint32_t nrf_802154_random_get(void)
{
	configASSERT(s_random_state != 0u);
	s_random_state ^= s_random_state << 13;
	s_random_state ^= s_random_state >> 17;
	s_random_state ^= s_random_state << 5;
	return s_random_state;
}

void nrf_802154_temperature_init(void)
{
}

void nrf_802154_temperature_deinit(void)
{
}

int8_t nrf_802154_temperature_get(void)
{
	return woz_freertos_die_temperature_c();
}
