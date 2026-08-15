/* SPDX-License-Identifier: ISC */

/*
 * The clock the USB stack asks for, answered by MPSL.
 *
 * app_usbd needs HFCLK running while the bus is active: the USBD peripheral
 * cannot clock its DMA off the internal oscillator, so a resume with the
 * crystal stopped enumerates and then drops packets. The SDK answers that with
 * nrf_drv_clock, which drives NRF_CLOCK directly and owns POWER_CLOCK.
 *
 * It cannot have either. peripherals.yml gives CLOCK to MPSL, and the reason is
 * not tidiness: MPSL arbitrates the crystal between the SoftDevice Controller
 * and the 802.15.4 driver with its own scheduler, and a third owner writing
 * TASKS_HFCLKSTOP behind its back stops the radio mid-event. So the request is
 * forwarded to the same API radio/nrf_802154_clock_freertos.c uses, and the
 * arbitration stays in one place.
 *
 * THE DECLARATIONS HERE ARE NOT THE SDK'S. They are the subset app_usbd
 * actually names -- the request/release pair, the started event, and the
 * handler item it hangs a callback off -- with the same names and shapes.
 * Reproducing the SDK's full header would be reproducing a driver this image
 * does not link.
 */
#ifndef NRF_DRV_CLOCK_H__
#define NRF_DRV_CLOCK_H__

#include <stdbool.h>

#include "sdk_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Only the one event is reachable. app_usbd asserts on anything else, and the
 * MPSL callback underneath filters to MPSL_CLOCK_EVT_HFCLK_STARTED before it
 * ever calls back, so the assertion cannot fire from here.
 */
typedef enum {
	NRF_DRV_CLOCK_EVT_HFCLK_STARTED,
} nrf_drv_clock_evt_type_t;

typedef void (*nrf_drv_clock_event_handler_t)(nrf_drv_clock_evt_type_t event);

/*
 * The SDK threads these items onto a list so several requesters can each be
 * called back. There is one requester in this image -- app_usbd -- so the
 * implementation keeps the most recent rather than a list, which is also what
 * MPSL does with its own callback.
 */
typedef struct nrf_drv_clock_handler_item_s {
	struct nrf_drv_clock_handler_item_s *p_next;
	nrf_drv_clock_event_handler_t event_handler;
} nrf_drv_clock_handler_item_t;

/**
 * Request HFCLK, calling @p p_handler_item's handler once it is running.
 *
 * Called back immediately and inline when the clock is already running, which
 * on this board is the normal case: the radio is up before USB is ever enabled.
 */
void nrf_drv_clock_hfclk_request(nrf_drv_clock_handler_item_t *p_handler_item);

/** Drop this layer's HFCLK request. The clock keeps running if the radio still
 *  wants it; that arbitration is MPSL's, not this file's. */
void nrf_drv_clock_hfclk_release(void);

/** True once MPSL is up, which is what "the clock driver is initialised" means
 *  here. app_usbd asserts on it before its first request. */
bool nrf_drv_clock_init_check(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF_DRV_CLOCK_H__ */
