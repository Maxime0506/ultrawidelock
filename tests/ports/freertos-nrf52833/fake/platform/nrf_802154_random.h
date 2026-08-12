#ifndef TEST_NRF_802154_RANDOM_H
#define TEST_NRF_802154_RANDOM_H

#include <stdint.h>

void nrf_802154_random_init(void);
void nrf_802154_random_deinit(void);
uint32_t nrf_802154_random_get(void);

#endif /* TEST_NRF_802154_RANDOM_H */
