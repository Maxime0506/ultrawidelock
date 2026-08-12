/**
 * @file
 * @brief The four board LEDs as one state display.
 *
 * On a board doing its job the four LEDs are the entire output. One LED per
 * question:
 *
 *   D9  green  the lock      solid = unlocked · one blip per 2 s = locked, alive
 *   D12 red    attention     solid = fault · 0.5 Hz = no fabric, needs commissioning
 *   D11 red    the phone     4 Hz = ranging · 1 Hz = Aliro session · off = idle
 *   D10 blue   a window      2 Hz = update window open · solid = provisioning mode
 *
 * D13 is not ours (DW3110 tx/rx), nor is D20 (J-Link OB). Each LED renders a
 * 16-slot bit pattern at 125 ms a slot off one timer; the tick stops itself when
 * every pattern is static. Nothing here blocks except status_led_boot_blink():
 * the tick runs on the system work queue eight times a second and must stay
 * cheap against the ~1836 us DW3110 reply-arm deadline.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "status_led.h"

#if IS_ENABLED(CONFIG_ULTRAWIDELOCK_DFU_RECEIVER)
#include "ultrawidelock_dfu_rx.h"
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(status_led, CONFIG_LOG_DEFAULT_LEVEL);

/* Which lamp answers which question. The aliases are the board's
 * (zephyr/boards/qorvo/decawave_dwm3001cdk): led0 D9 green, led1 D12 red,
 * led2 D11 red, led3 D10 blue. All four are ACTIVE_LOW; gpio_pin_set_dt() takes
 * the logical level and the devicetree flag handles the inversion. */
enum led_id {
	LED_LOCK,   /* led0, D9 green */
	LED_ATTN,   /* led1, D12 red */
	LED_RADIO,  /* led2, D11 red */
	LED_WINDOW, /* led3, D10 blue */
	LED_COUNT,
};

static const struct gpio_dt_spec s_leds[LED_COUNT] = {
	[LED_LOCK] = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	[LED_ATTN] = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	[LED_RADIO] = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
	[LED_WINDOW] = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios),
};

/* One slot of the 16-slot cycle. 125 ms x 16 = a 2 s period, which is the
 * slowest thing this display shows and the resolution of the fastest. */
#define SLOT_MS  125
#define SLOT_NUM 16

/* Read MSB first, so the leftmost bit of each literal is the first 125 ms. */
#define PAT_OFF       0x0000u /* dark */
#define PAT_SOLID     0xFFFFu /* lit */
#define PAT_BLIP      0x8000u /* 125 ms in every 2 s: a heartbeat, not a request */
#define PAT_HALF_HZ   0xFF00u /* 1 s on, 1 s off */
#define PAT_ONE_HZ    0xF0F0u
#define PAT_TWO_HZ    0xCCCCu
#define PAT_FOUR_HZ   0xAAAAu

/* The locked-and-healthy pattern. Compiling the heartbeat out is what lets an
 * idle board stop the tick entirely, so it is a real knob and not a preference. */
#if IS_ENABLED(CONFIG_ALIRO_STATUS_LED_HEARTBEAT)
#define PAT_IDLE PAT_BLIP
#else
#define PAT_IDLE PAT_OFF
#endif

static atomic_t s_signals;
/* Which LEDs configured. A board that lost one must still unlock a door, so a
 * missing lamp is dropped rather than failed on. */
static uint8_t s_ready;
/* Written by the tick and reset by status_led_signal(); see the comment there
 * for why the race between them is harmless. */
static uint8_t s_phase;

static void tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_tick, tick);

/**
 * Map the asserted signals onto one pattern per LED.
 *
 * The whole display is re-mapped here and nowhere else. Within an LED the tests
 * are ordered most-specific first, which is also most-urgent first: ranging
 * cannot happen without a session, and a fault outranks a board that merely
 * wants commissioning.
 */
static void render(uint32_t sig, uint16_t pat[LED_COUNT])
{
	pat[LED_LOCK] = (sig & STATUS_LED_UNLOCKED) ? PAT_SOLID : PAT_IDLE;

	pat[LED_ATTN] = (sig & STATUS_LED_FAULT)		? PAT_SOLID
			: (sig & STATUS_LED_UNCOMMISSIONED) ? PAT_HALF_HZ
							    : PAT_OFF;

	pat[LED_RADIO] = (sig & STATUS_LED_RANGING)   ? PAT_FOUR_HZ
			 : (sig & STATUS_LED_SESSION) ? PAT_ONE_HZ
						      : PAT_OFF;

	pat[LED_WINDOW] = (sig & STATUS_LED_DFU_WINDOW)	 ? PAT_TWO_HZ
			  : (sig & STATUS_LED_PROV_MODE) ? PAT_SOLID
							 : PAT_OFF;
}

/**
 * Drive every LED to its level for the current slot, advance the phase, and
 * reschedule only if something still moves.
 *
 * Stopping on an all-static frame is what keeps an idle board off this timer:
 * a static pattern has the same level in all sixteen slots, so the levels just
 * written are already correct for every slot that will not now happen, and the
 * next status_led_signal() restarts the tick.
 */
static void tick(struct k_work *work)
{
	uint16_t pat[LED_COUNT];
	bool moving = false;

	ARG_UNUSED(work);

	render((uint32_t)atomic_get(&s_signals), pat);

	for (uint8_t i = 0u; i < (uint8_t)LED_COUNT; i++) {
		if ((s_ready & BIT(i)) != 0u) {
			(void)gpio_pin_set_dt(&s_leds[i],
					      (pat[i] >> (SLOT_NUM - 1u - s_phase)) & 1u);
		}
		moving = moving || (pat[i] != PAT_OFF && pat[i] != PAT_SOLID);
	}

	s_phase = (uint8_t)((s_phase + 1u) % SLOT_NUM);

	if (moving) {
		(void)k_work_reschedule(&s_tick, K_MSEC(SLOT_MS));
	}
}

void status_led_signal(uint32_t signals, bool on)
{
	atomic_val_t before = on ? atomic_or(&s_signals, (atomic_val_t)signals)
				 : atomic_and(&s_signals, (atomic_val_t)~signals);
	atomic_val_t after = on ? (before | (atomic_val_t)signals)
				: (before & (atomic_val_t)~signals);

	if (before == after) {
		return;
	}

	/*
	 * Restart the cycle so the new state shows within a millisecond instead
	 * of up to a slot later. That matters for exactly one caller and it is
	 * the button: a quarter second of nothing after a press is the gap that
	 * makes someone press again.
	 *
	 * s_phase is also written by the tick, which runs on the system work
	 * queue and can be preempted here. The worst outcome is that this reset
	 * is overwritten and the change waits one 125 ms slot -- the same delay
	 * as not having this at all, on a frame nobody was watching yet. Not
	 * worth a lock on a part at 96% RAM.
	 */
	s_phase = 0u;
	(void)k_work_reschedule(&s_tick, K_NO_WAIT);
}

void status_led_boot_blink(void)
{
	struct k_work_sync sync;

	if ((s_ready & BIT(LED_LOCK)) == 0u) {
		return;
	}

	/* Owning the pin outright for the duration, because the tick would
	 * otherwise overwrite every toggle within 125 ms and the blink would
	 * come out as whatever the heartbeat was already doing. The _sync form
	 * because the plain cancel returns while a frame already dispatched is
	 * still running, and that frame would land in the middle of this. Legal
	 * here and only here: this runs on main, never on the work queue. */
	(void)k_work_cancel_delayable_sync(&s_tick, &sync);

	(void)gpio_pin_set_dt(&s_leds[LED_LOCK], 1);
	for (int i = 0; i < 6; i++) {
		k_sleep(K_MSEC(120));
		(void)gpio_pin_toggle_dt(&s_leds[LED_LOCK]);
	}
	(void)gpio_pin_set_dt(&s_leds[LED_LOCK], 0);

	s_phase = 0u;
	(void)k_work_reschedule(&s_tick, K_NO_WAIT);
}

#if IS_ENABLED(CONFIG_ULTRAWIDELOCK_DFU_RECEIVER)
/**
 * Follow the update window rather than the button that opened it, so the light
 * goes out when the five minutes expire on their own.
 */
static void window_changed(bool open)
{
	status_led_signal(STATUS_LED_DFU_WINDOW, open);
}
#endif

/**
 * Configure every LED the board actually has, subscribe to the update window if
 * this image has a DFU receiver, and start the display.
 *
 * APPLICATION level: the GPIO driver is up by then, and nothing can assert a
 * signal before the radios start anyway. Never fails the boot -- a board that
 * will not blink must still unlock a door.
 */
static int status_led_init(void)
{
	for (uint8_t i = 0u; i < (uint8_t)LED_COUNT; i++) {
		if (gpio_is_ready_dt(&s_leds[i]) &&
		    gpio_pin_configure_dt(&s_leds[i], GPIO_OUTPUT_INACTIVE) == 0) {
			s_ready |= BIT(i);
		}
	}
	if (s_ready != (uint8_t)BIT_MASK(LED_COUNT)) {
		LOG_WRN("status LEDs available: 0x%02x of 0x%02x; this board will "
			"under-report itself",
			s_ready, (unsigned int)BIT_MASK(LED_COUNT));
	}

#if IS_ENABLED(CONFIG_ULTRAWIDELOCK_DFU_RECEIVER)
	ultrawidelock_dfu_set_window_cb(window_changed);
#endif

	(void)k_work_reschedule(&s_tick, K_NO_WAIT);
	return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
