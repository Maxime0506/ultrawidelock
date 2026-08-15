/* SPDX-License-Identifier: ISC */

/*
 * The three USB supply events, read off a vector MPSL owns.
 *
 * The USBD peripheral cannot be enabled on a whim: the sequence is fixed by the
 * part. USBDETECTED says a cable was plugged in and the regulator was started;
 * USBPWRRDY says the regulator has settled and the peripheral may be enabled;
 * USBREMOVED says the cable is gone and everything must be torn down. Enabling
 * USBD before READY is the classic nRF52 USB bug -- it enumerates on a bench
 * supply and fails on a laptop, because the difference is how fast VBUS rises.
 *
 * All three arrive on POWER_CLOCK, which peripherals.yml gives to MPSL. The
 * SDK's nrf_drv_power would install its own handler on that vector and enable
 * its own interrupts there, which is the same collision nrf_drv_clock would
 * have caused. So this header keeps the shape app_usbd expects and
 * usb_power_freertos.c reads the events out of the fan-out the port already
 * routes through MPSL's handler.
 *
 * Only the USB half of nrf_drv_power is here. The SDK's driver also covers
 * power-fail warning and sleep events, and this image wants neither: POF would
 * be a second writer of the same registers, and there is no sleep mode a lock
 * with a radio up can enter.
 */
#ifndef NRF_DRV_POWER_H__
#define NRF_DRV_POWER_H__

#include "sdk_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	NRF_DRV_POWER_USB_EVT_DETECTED, /**< VBUS present; the regulator has started */
	NRF_DRV_POWER_USB_EVT_REMOVED,  /**< VBUS gone */
	NRF_DRV_POWER_USB_EVT_READY,    /**< the regulator has settled; USBD may be enabled */
} nrf_drv_power_usb_evt_t;

typedef void (*nrf_drv_power_usb_event_handler_t)(nrf_drv_power_usb_evt_t event);

typedef struct {
	nrf_drv_power_usb_event_handler_t handler;
} nrf_drv_power_usbevt_config_t;

/**
 * Initialise the power driver.
 *
 * @p p_config is ignored and app_usbd passes NULL: everything the SDK's config
 * carries -- power-fail warning, DC/DC, sleep events -- belongs to MPSL on this
 * board. Present because app_usbd calls it, and it returns success so that the
 * NRF_ERROR_MODULE_ALREADY_INITIALIZED path it also accepts is never needed.
 */
ret_code_t nrf_drv_power_init(void const *p_config);

/** Start delivering USB supply events to @p p_config->handler. */
ret_code_t nrf_drv_power_usbevt_init(nrf_drv_power_usbevt_config_t const *p_config);

/** Stop delivering them, and mask the events in hardware. */
void nrf_drv_power_usbevt_uninit(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF_DRV_POWER_H__ */
