/*
 * The one thing the 802.15.4 type header takes from the RADIO HAL. The real
 * header needs the full MDK device definition, which the host build has no
 * business pulling in: the pinned radio platform names no RADIO register at
 * all, only this enumeration by way of nrf_802154_types.h.
 */
#ifndef TEST_HAL_NRF_RADIO_H
#define TEST_HAL_NRF_RADIO_H

#include <stdint.h>

typedef uint8_t nrf_radio_cca_mode_t;

#define NRF_RADIO_CCA_MODE_ED 0x00
#define NRF_RADIO_CCA_MODE_CARRIER 0x01
#define NRF_RADIO_CCA_MODE_CARRIER_AND_ED 0x02
#define NRF_RADIO_CCA_MODE_CARRIER_OR_ED 0x03

#endif /* TEST_HAL_NRF_RADIO_H */
