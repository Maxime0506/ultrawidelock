/**
 * @file
 * @brief Show that the update window is open, on the board itself.
 *
 * The window is the entire authorization model for an update, and until now it
 * was invisible. Three things open it -- SW2, Apple Home's "Turn On Pairing
 * Mode", and the bench SWD write -- and none of them gave the board any way to
 * say so. An owner who pressed the button could not tell whether the press had
 * registered, and the five minutes could run out while they were still finding
 * the phone. The only feedback was a log line on a debugger that a released
 * board does not have attached.
 *
 * D10, the blue one, at 2 Hz. Blue because the other three are the DW3000's own
 * colours by convention on this board (D13 is tx red / rx green) and a fourth
 * red would read as a fault; 2 Hz because a slower heartbeat reads as "alive"
 * rather than "waiting for you", which is the wrong message for something that
 * expires.
 *
 * It follows the window rather than the button, so it is honest about the state
 * that actually matters: it goes out when the window expires on its own, not
 * when someone stops pressing.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "woz_dfu_rx.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(status_led, CONFIG_LOG_DEFAULT_LEVEL);

/* All four LEDs on this board are ACTIVE_LOW; gpio_pin_set_dt() takes the
 * logical level and the devicetree flag handles the inversion. */
static const struct gpio_dt_spec s_led = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);

#define BLINK_MS 250

static void blink(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_blink, blink);
static bool s_lit;

/**
 * Toggle the status LED and reschedule itself at BLINK_MS intervals.
 */
static void blink(struct k_work *work)
{
	ARG_UNUSED(work);

	s_lit = !s_lit;
	(void)gpio_pin_set_dt(&s_led, s_lit ? 1 : 0);
	(void)k_work_reschedule(&s_blink, K_MSEC(BLINK_MS));
}

/**
 * Set the status LED lit immediately and start blinking on a window open; cancel blinking and turn
 * it off on window close.
 */
static void window_changed(bool open)
{
	if (!gpio_is_ready_dt(&s_led)) {
		return;
	}

	if (open) {
		/* Lit immediately rather than on the first timer tick: the
		 * point is to confirm a button press, and a quarter second of
		 * nothing is exactly the gap that makes someone press again. */
		s_lit = true;
		(void)gpio_pin_set_dt(&s_led, 1);
		(void)k_work_reschedule(&s_blink, K_MSEC(BLINK_MS));
	} else {
		(void)k_work_cancel_delayable(&s_blink);
		s_lit = false;
		(void)gpio_pin_set_dt(&s_led, 0);
	}
}

/**
 * Initialize the status LED on GPIO and register the DFU window state change callback; logs a
 * warning but does not fail if the LED is not ready.
 */
static int status_led_init(void)
{
	if (!gpio_is_ready_dt(&s_led)) {
		LOG_WRN("D10 is not ready; the update window will be invisible");
		return 0;
	}
	if (gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_WRN("D10 would not configure; the update window will be invisible");
		return 0;
	}

	woz_dfu_set_window_cb(window_changed);
	return 0;
}

/* APPLICATION level: the GPIO driver is up by then, and nothing can open a
 * window before the radio starts anyway. Never fails the boot -- a board that
 * will not blink must still unlock a door. */
SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
