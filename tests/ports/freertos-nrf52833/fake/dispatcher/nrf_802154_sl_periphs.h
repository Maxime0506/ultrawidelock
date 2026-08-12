/*
 * The one definition the vendor high-precision timer platform takes from
 * nrfxlib's peripheral map. peripherals.yml freezes TIMER1 to the 802.15.4
 * driver, and scripts/freertos-radio-source-check.sh asserts that instance
 * number is still upstream's default.
 */
#ifndef TEST_NRF_802154_SL_PERIPHS_H
#define TEST_NRF_802154_SL_PERIPHS_H

#define NRF_802154_HIGH_PRECISION_TIMER_INSTANCE_NO 1
#define NRF_802154_HIGH_PRECISION_TIMER_INSTANCE NRF_TIMER1

#endif /* TEST_NRF_802154_SL_PERIPHS_H */
