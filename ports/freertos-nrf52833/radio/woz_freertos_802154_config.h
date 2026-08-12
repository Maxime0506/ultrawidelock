/*
 * The nRF 802.15.4 driver's configuration for this port.
 *
 * Forced in by NRF_802154_PROJECT_CONFIG rather than selected by macro, so
 * there is one answer to what the driver was built with and it is a file in
 * this tree. Everything not named here keeps the driver's own default from
 * common/include/nrf_802154_config.h.
 *
 * The peripheral choices below are not free: they must agree with
 * board/peripherals.yml and with what MPSL and the SoftDevice Controller
 * reserve, because all three share one PPI and EGU space. A collision here is
 * silent -- two owners of one channel produce a radio that works until the
 * other stack happens to be active.
 */
#ifndef WOZ_FREERTOS_802154_CONFIG_H
#define WOZ_FREERTOS_802154_CONFIG_H

/*
 * MIN and MAX.
 *
 * nrf_802154_core.c uses both and defines neither: under Zephyr they arrive
 * from <zephyr/sys/util.h>, which every nrfxlib translation unit sees through
 * the Zephyr integration. This port has no such header, and this config is
 * included by nrf_802154_config.h into every driver source, which makes it the
 * one place the definitions reach all of them without touching vendor code.
 *
 * Guarded because one vendor source (nrf_802154_aes_ccm_acc_ecb.c) defines MIN
 * for itself. Double-evaluating arguments matches Zephyr's own definition, so
 * the driver's uses are already written for it.
 */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/*
 * The receive pool, and the largest single RAM line item this driver adds.
 *
 * Each buffer holds a full 127-byte frame plus its metadata, and the pool is
 * static. Twenty is Nordic's default for a Thread end device and is kept here
 * so the first measurement is against the configuration a Zephyr build would
 * have had; it is a number to revisit with a real RAM figure, not a number to
 * guess downward before one exists.
 */
#define NRF_802154_RX_BUFFERS 20

/*
 * This product is a sleepy end device: it never routes, so it never needs to
 * receive while another node's transmission is in flight on a second context.
 */
#define NRF_802154_PENDING_SHORT_ADDRESSES  16
#define NRF_802154_PENDING_EXTENDED_ADDRESSES 16

/*
 * CSMA-CA, delayed TRX and IFS stay at their defaults (enabled): OpenThread's
 * MAC drives all three, and turning any of them off here would not shrink the
 * image so much as break the MAC above it.
 */

/*
 * Frame timestamps are required. OpenThread's CSL and its ACK timing both read
 * them, and the service layer provides the timer that produces them.
 */
#define NRF_802154_FRAME_TIMESTAMP_ENABLED 1

#endif /* WOZ_FREERTOS_802154_CONFIG_H */
