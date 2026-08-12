/* Subset of the pinned nrfxlib sl/include/platform/nrf_802154_clock.h. */
#ifndef TEST_NRF_802154_CLOCK_H
#define TEST_NRF_802154_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

void nrf_802154_clock_init(void);
void nrf_802154_clock_deinit(void);

void nrf_802154_clock_hfclk_start(void);
void nrf_802154_clock_hfclk_stop(void);
bool nrf_802154_clock_hfclk_is_running(void);

void nrf_802154_clock_lfclk_start(void);
void nrf_802154_clock_lfclk_stop(void);
bool nrf_802154_clock_lfclk_is_running(void);

/* Implemented by the driver; the port calls them. */
extern void nrf_802154_clock_hfclk_ready(void);
extern void nrf_802154_clock_lfclk_ready(void);

#endif /* TEST_NRF_802154_CLOCK_H */
