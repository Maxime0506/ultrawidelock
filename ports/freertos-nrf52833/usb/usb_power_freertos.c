/*
 * What the USB stack asks the board for: the supply events, and the crystal.
 *
 * This is the whole of usb_compat/nrf_drv_power.h and usb_compat/nrf_drv_clock.h.
 * Both exist because the SDK's own drivers for them would take POWER_CLOCK,
 * which peripherals.yml gives to MPSL -- see those two headers for why that is
 * not negotiable. What is left after the ownership question is small: three
 * event bits and a forwarded clock request.
 *
 * PRIORITY, stated once because it governs everything below. The POWER events
 * arrive on MPSL's vector at priority 0, which is above the FreeRTOS syscall
 * ceiling of 4. Nothing in the interrupt path here may call a FreeRTOS API. It
 * does not: the handler reads three event flags and calls app_usbd's power
 * callback, which pushes into an nrf_atfifo and returns.
 *
 * That last point is where this build knowingly differs from the SDK's
 * assumption, so it is written down rather than left to be discovered.
 * app_usbd.c asserts USBD_CONFIG_IRQ_PRIORITY == POWER_CONFIG_IRQ_PRIORITY when
 * its event queue is enabled, and sdk_config.h sets both to 6 -- so the
 * assertion passes while the hardware disagrees, because POWER really runs at
 * MPSL's 0. An assertion that passes for the wrong reason is worth no more than
 * one that cannot fire, so here is the argument it was standing in for: the
 * queue is nrf_atfifo, whose push and pop are LDREX/STREX retry loops
 * (nrf_atfifo_internal.h), not critical sections. A lock-free CAS loop is
 * correct across two different interrupt priorities on the same core, which is
 * the property Nordic's equal-priority rule was buying more cheaply. The two
 * priorities cannot be equalised anyway: USB cannot run at 0 without delaying
 * the radio, and MPSL cannot run at 6.
 */
#include <stdbool.h>
#include <stddef.h>

#include <hal/nrf_power.h>
#include <nrfx.h>

#include <mpsl_clock.h>

#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"

#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_radio.h"

#define TAG "usb_power"

/* ---- the supply events --------------------------------------------------- */

static nrf_drv_power_usb_event_handler_t s_usb_handler;

#define USB_EVENT_MASK                                                                             \
	(NRF_POWER_INT_USBDETECTED_MASK | NRF_POWER_INT_USBREMOVED_MASK |                          \
	 NRF_POWER_INT_USBPWRRDY_MASK)

/*
 * Runs at priority 0, after MPSL. Each event is checked and cleared
 * individually because all three can be pending at once: a cable plugged in
 * while the last one's REMOVED had not been serviced produces DETECTED and
 * PWRRDY in the same entry, and dispatching only the first would leave the
 * stack waiting for a regulator that is already ready.
 */
static void usb_power_isr(void)
{
	if (nrf_power_event_check(NRF_POWER_EVENT_USBDETECTED)) {
		nrf_power_event_clear(NRF_POWER_EVENT_USBDETECTED);
		if (s_usb_handler != NULL) {
			s_usb_handler(NRF_DRV_POWER_USB_EVT_DETECTED);
		}
	}
	if (nrf_power_event_check(NRF_POWER_EVENT_USBPWRRDY)) {
		nrf_power_event_clear(NRF_POWER_EVENT_USBPWRRDY);
		if (s_usb_handler != NULL) {
			s_usb_handler(NRF_DRV_POWER_USB_EVT_READY);
		}
	}
	if (nrf_power_event_check(NRF_POWER_EVENT_USBREMOVED)) {
		nrf_power_event_clear(NRF_POWER_EVENT_USBREMOVED);
		if (s_usb_handler != NULL) {
			s_usb_handler(NRF_DRV_POWER_USB_EVT_REMOVED);
		}
	}
}

ret_code_t nrf_drv_power_init(void const *p_config)
{
	/*
	 * Everything the SDK's config carries -- DC/DC, power-fail warning,
	 * sleep events -- is MPSL's on this board, and app_usbd passes NULL
	 * anyway. Kept as a parameter so the vendor source needs no edit.
	 */
	(void)p_config;
	return NRF_SUCCESS;
}

ret_code_t nrf_drv_power_usbevt_init(nrf_drv_power_usbevt_config_t const *p_config)
{
	if (p_config == NULL || p_config->handler == NULL) {
		return NRF_ERROR_INVALID_PARAM;
	}
	if (ultrawidelock_freertos_radio_set_power_handler(usb_power_isr) != 0) {
		ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "POWER vector already claimed");
		return NRF_ERROR_INVALID_STATE;
	}

	s_usb_handler = p_config->handler;

	/*
	 * Cleared before they are unmasked. A cable already plugged in when this
	 * runs has left DETECTED and PWRRDY latched from before the handler
	 * existed, and delivering those two now would be correct but would
	 * arrive out of order with the state check below.
	 */
	nrf_power_event_clear(NRF_POWER_EVENT_USBDETECTED);
	nrf_power_event_clear(NRF_POWER_EVENT_USBPWRRDY);
	nrf_power_event_clear(NRF_POWER_EVENT_USBREMOVED);
	nrf_power_int_enable(USB_EVENT_MASK);

	/*
	 * The cable that was already there.
	 *
	 * This board's console is reached by plugging in and THEN holding the
	 * button through reset, so the ordinary case is that VBUS is present
	 * before this line runs and the two events have already been and gone.
	 * Without this the stack would sit waiting for a DETECTED that cannot
	 * arrive again until the cable is pulled -- which is exactly the bug
	 * that reads as "the console works on the second plug".
	 */
	if (nrf_power_usbregstatus_vbusdet_get()) {
		s_usb_handler(NRF_DRV_POWER_USB_EVT_DETECTED);
		if (nrf_power_usbregstatus_outrdy_get()) {
			s_usb_handler(NRF_DRV_POWER_USB_EVT_READY);
		}
	}
	return NRF_SUCCESS;
}

void nrf_drv_power_usbevt_uninit(void)
{
	nrf_power_int_disable(USB_EVENT_MASK);
	s_usb_handler = NULL;
	/*
	 * The registered vector handler is deliberately not withdrawn. It is
	 * harmless with no handler behind it, and the alternative -- a slot that
	 * can be released and re-taken -- is state this board has no use for:
	 * nothing else watches POWER, and USB is brought up once per boot.
	 */
}

/* ---- the crystal --------------------------------------------------------- */

/*
 * One source on this part. Written as the constant the 802.15.4 clock platform
 * uses rather than repeated, because the two are requesting the same crystal
 * and a disagreement here would be a second arbitration.
 */
#define ULTRAWIDELOCK_FREERTOS_USB_HF_SRC MPSL_CLOCK_HF_SRC_XO

static nrf_drv_clock_handler_item_t *s_clock_item;
static bool s_clock_requested;

/* Runs in the MPSL low-priority context, which is the shared worker task. */
static void hfclk_event(mpsl_clock_evt_type_t evt_type)
{
	nrf_drv_clock_handler_item_t *item = s_clock_item;

	if (evt_type != MPSL_CLOCK_EVT_HFCLK_STARTED) {
		return;
	}
	if (item != NULL && item->event_handler != NULL) {
		item->event_handler(NRF_DRV_CLOCK_EVT_HFCLK_STARTED);
	}
}

void nrf_drv_clock_hfclk_request(nrf_drv_clock_handler_item_t *p_handler_item)
{
	uint32_t running = 0;

	s_clock_item = p_handler_item;

	if (!s_clock_requested) {
		if (mpsl_clock_hfclk_src_request(ULTRAWIDELOCK_FREERTOS_USB_HF_SRC, hfclk_event) != 0) {
			ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, TAG, "hfclk request refused");
			return;
		}
		s_clock_requested = true;
	}

	/*
	 * Already running is the normal case: the radio is up long before USB is
	 * enabled, so MPSL grants this from a clock it never stopped and has no
	 * started event left to deliver. Calling back inline is what the SDK's
	 * driver does in the same situation, and without it the stack waits for
	 * an event that will not come.
	 */
	if (mpsl_clock_hfclk_src_is_running(ULTRAWIDELOCK_FREERTOS_USB_HF_SRC, &running) == 0 &&
	    running != 0) {
		hfclk_event(MPSL_CLOCK_EVT_HFCLK_STARTED);
	}
}

void nrf_drv_clock_hfclk_release(void)
{
	if (!s_clock_requested) {
		return;
	}
	s_clock_requested = false;
	s_clock_item = NULL;
	/*
	 * Releases this layer's claim only. The crystal keeps running for as
	 * long as the controller or the 802.15.4 driver still wants it, which is
	 * MPSL's accounting and the reason none of the three touches
	 * TASKS_HFCLKSTOP itself.
	 */
	(void)mpsl_clock_hfclk_src_release(ULTRAWIDELOCK_FREERTOS_USB_HF_SRC);
}

bool nrf_drv_clock_init_check(void)
{
	/*
	 * "Is there a clock driver" reads here as "is MPSL up", because MPSL is
	 * the clock driver. app_usbd asserts on this before its first request,
	 * and a false answer means the radio has not started -- which on this
	 * board is fatal long before USB is reached.
	 */
	return ultrawidelock_freertos_radio_ready();
}
