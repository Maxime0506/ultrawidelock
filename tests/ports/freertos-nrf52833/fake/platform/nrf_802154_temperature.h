#ifndef TEST_NRF_802154_TEMPERATURE_H
#define TEST_NRF_802154_TEMPERATURE_H

#include <stdint.h>

void nrf_802154_temperature_init(void);
void nrf_802154_temperature_deinit(void);
int8_t nrf_802154_temperature_get(void);

#endif /* TEST_NRF_802154_TEMPERATURE_H */
