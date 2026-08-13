/*
 * The four board LEDs as one state display.
 *
 * On a board doing its job the four LEDs are the entire output. One LED per
 * question:
 *
 *   D9  green  the lock      solid = unlocked · one blip per 2 s = locked, alive
 *   D12 red    attention     solid = fault · 0.5 Hz = no fabric, needs commissioning
 *   D11 red    the phone     4 Hz = ranging · 1 Hz = Aliro session · off = idle
 *   D10 blue   a window      2 Hz = update window open · solid = provisioning mode
 *
 * Each LED renders a 16-slot bit pattern at 125 ms a slot off one delayable
 * work item; the tick stops itself when every pattern is static. Nothing here
 * blocks except status_led_boot_blink(): the tick runs on the OSAL work queue
 * eight times a second and must stay cheap against the ~1836 us DW3110
 * reply-arm deadline.
 *
 * A LINE-FOR-LINE MIRROR of apps/dwm3001cdk-lock/src/status_led.c, including
 * the patterns and the order of the tests in render(), because the two images
 * run on the same board in front of the same person. Three things differ, and
 * all three are the absence of Zephyr rather than a change of mind:
 *
 *  - The pins are named constants, not devicetree nodes, and the active-low
 *    inversion is written out here. gpio_pin_set_dt() reads GPIO_ACTIVE_LOW
 *    from the DT flags and inverts for its caller; nrf_gpio_pin_write() does
 *    not, so a straight port of the Zephyr line would light the board's LEDs
 *    exactly wrong and still pass every test that only counts writes.
 *
 *  - Signals are held under a critical section rather than in an atomic_t.
 *    The bits are set and cleared from several tasks but never from an
 *    interrupt, and the region is two instructions long.
 *
 *  - Start is a call from the boot task, not SYS_INIT. The work queue does not
 *    exist before the scheduler.
 */
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_gpio.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_board.h>
#include <woz_freertos_platform.h>
#include <woz_osal.h>

#include "status_led.h"

#include <woz_dfu_rx.h>

#define LED_TAG "status_led"

/* Which lamp answers which question. */
enum led_id {
	LED_LOCK,   /* D9 green */
	LED_ATTN,   /* D12 red */
	LED_RADIO,  /* D11 red */
	LED_WINDOW, /* D10 blue */
	LED_COUNT,
};

static const uint32_t s_led_pin[LED_COUNT] = {
	[LED_LOCK] = WOZ_FREERTOS_PIN_LED_LOCK,
	[LED_ATTN] = WOZ_FREERTOS_PIN_LED_ATTN,
	[LED_RADIO] = WOZ_FREERTOS_PIN_LED_RADIO,
	[LED_WINDOW] = WOZ_FREERTOS_PIN_LED_WINDOW,
};

/*
 * One slot of the 16-slot cycle. 125 ms x 16 = a 2 s period, which is the
 * slowest thing this display shows and the resolution of the fastest.
 */
#define SLOT_MS 125
#define SLOT_NUM 16

/* Read MSB first, so the leftmost bit of each literal is the first 125 ms. */
#define PAT_OFF 0x0000u     /* dark */
#define PAT_SOLID 0xFFFFu   /* lit */
#define PAT_BLIP 0x8000u    /* 125 ms in every 2 s: a heartbeat, not a request */
#define PAT_HALF_HZ 0xFF00u /* 1 s on, 1 s off */
#define PAT_ONE_HZ 0xF0F0u
#define PAT_TWO_HZ 0xCCCCu
#define PAT_FOUR_HZ 0xAAAAu

/*
 * The locked-and-healthy pattern. Compiling the heartbeat out is what lets an
 * idle board stop the tick entirely, so it is a real knob and not a preference:
 * with every LED static the tick stops itself. It costs the one signal that
 * separates "locked" from "dead", which are the same picture without it.
 */
#ifndef WOZ_FREERTOS_STATUS_LED_HEARTBEAT
#define WOZ_FREERTOS_STATUS_LED_HEARTBEAT 1
#endif
#if WOZ_FREERTOS_STATUS_LED_HEARTBEAT
#define PAT_IDLE PAT_BLIP
#else
#define PAT_IDLE PAT_OFF
#endif

static uint32_t s_signals;
/*
 * Which LEDs are up. Every pin on this board is present and configuring one
 * cannot fail, so this is all four in practice -- it is kept because the
 * display must not assume that, and because a future board may wire fewer.
 */
static uint8_t s_ready;
/* Written by the tick and reset by status_led_signal(); see the note there. */
static uint8_t s_phase;

static struct woz_dwork s_tick;

/*
 * Drive one lamp. THE INVERSION LIVES HERE AND NOWHERE ELSE: all four LEDs
 * are wired active low, so a logical 1 -- lit -- is a low pin.
 */
static void led_write(enum led_id id, bool lit)
{
	nrf_gpio_pin_write(s_led_pin[id], lit ? 0u : 1u);
}

/*
 * Map the asserted signals onto one pattern per LED.
 *
 * The whole display is re-mapped here and nowhere else. Within an LED the
 * tests are ordered most-specific first, which is also most-urgent first:
 * ranging cannot happen without a session, and a fault outranks a board that
 * merely wants commissioning.
 */
static void render(uint32_t sig, uint16_t pat[LED_COUNT])
{
	pat[LED_LOCK] = (sig & STATUS_LED_UNLOCKED) ? PAT_SOLID : PAT_IDLE;

	pat[LED_ATTN] = (sig & STATUS_LED_FAULT)	  ? PAT_SOLID
			: (sig & STATUS_LED_UNCOMMISSIONED) ? PAT_HALF_HZ
							    : PAT_OFF;

	pat[LED_RADIO] = (sig & STATUS_LED_RANGING)   ? PAT_FOUR_HZ
			 : (sig & STATUS_LED_SESSION) ? PAT_ONE_HZ
						      : PAT_OFF;

	pat[LED_WINDOW] = (sig & STATUS_LED_DFU_WINDOW)	 ? PAT_TWO_HZ
			  : (sig & STATUS_LED_PROV_MODE) ? PAT_SOLID
							 : PAT_OFF;
}

static uint32_t signals_get(void)
{
	uint32_t sig;

	taskENTER_CRITICAL();
	sig = s_signals;
	taskEXIT_CRITICAL();
	return sig;
}

/*
 * Drive every LED to its level for the current slot, advance the phase, and
 * reschedule only if something still moves.
 *
 * Stopping on an all-static frame is what keeps an idle board off this timer:
 * a static pattern has the same level in all sixteen slots, so the levels just
 * written are already correct for every slot that will not now happen, and the
 * next status_led_signal() restarts the tick.
 */
static void tick(struct woz_dwork *dwork)
{
	uint16_t pat[LED_COUNT];
	bool moving = false;

	(void)dwork;

	render(signals_get(), pat);

	for (uint8_t i = 0u; i < (uint8_t)LED_COUNT; i++) {
		if ((s_ready & (1u << i)) != 0u) {
			led_write((enum led_id)i,
				  ((pat[i] >> (SLOT_NUM - 1u - s_phase)) & 1u) != 0u);
		}
		moving = moving || (pat[i] != PAT_OFF && pat[i] != PAT_SOLID);
	}

	s_phase = (uint8_t)((s_phase + 1u) % SLOT_NUM);

	if (moving) {
		(void)woz_dwork_reschedule(&s_tick, SLOT_MS);
	}
}

void status_led_signal(uint32_t signals, bool on)
{
	uint32_t before;
	uint32_t after;

	taskENTER_CRITICAL();
	before = s_signals;
	s_signals = on ? (before | signals) : (before & ~signals);
	after = s_signals;
	taskEXIT_CRITICAL();

	if (before == after) {
		return;
	}

	/*
	 * Restart the cycle so the new state shows within a millisecond instead
	 * of up to a slot later. That matters for exactly one caller and it is
	 * the button: a quarter second of nothing after a press is the gap that
	 * makes someone press again.
	 *
	 * s_phase is also written by the tick, which runs on the work queue and
	 * can be preempted here. The worst outcome is that this reset is
	 * overwritten and the change waits one 125 ms slot -- the same delay as
	 * not having this at all, on a frame nobody was watching yet. Not worth
	 * a lock on a part at 96% RAM.
	 */
	s_phase = 0u;
	(void)woz_dwork_reschedule(&s_tick, 0);
}

void status_led_boot_blink(void)
{
	if ((s_ready & (1u << LED_LOCK)) == 0u) {
		return;
	}

	/*
	 * Owning the pin outright for the duration, because the tick would
	 * otherwise overwrite every toggle within 125 ms and the blink would
	 * come out as whatever the heartbeat was already doing.
	 *
	 * Unlike the Zephyr side there is no cancel-and-wait: woz_dwork_cancel
	 * disarms the timer but a frame already running on the work queue runs
	 * to completion. It would land in the middle of this blink and cost one
	 * of the six edges. Accepted rather than papered over, because the
	 * alternative is a synchronising primitive the OSAL does not have, and
	 * because this is feedback for a button press, not a signal anything
	 * reads back.
	 */
	(void)woz_dwork_cancel(&s_tick);

	led_write(LED_LOCK, true);
	for (int i = 0; i < 6; i++) {
		vTaskDelay(pdMS_TO_TICKS(120));
		led_write(LED_LOCK, (i % 2) != 0);
	}
	led_write(LED_LOCK, false);

	s_phase = 0u;
	(void)woz_dwork_reschedule(&s_tick, 0);
}

/*
 * Follow the update window rather than the button that opened it, so the light
 * goes out when the window expires on its own.
 */
static void window_changed(bool open)
{
	status_led_signal(STATUS_LED_DFU_WINDOW, open);
}

void status_led_start(void)
{
	for (uint8_t i = 0u; i < (uint8_t)LED_COUNT; i++) {
		/*
		 * Dark before it is an output. Configuring first would drive
		 * whatever OUT happened to hold, which on a cold boot is 0 --
		 * and 0 is lit on an active-low lamp, so every LED would flash
		 * before the display had decided anything.
		 */
		led_write((enum led_id)i, false);
		nrf_gpio_cfg_output(s_led_pin[i]);
		s_ready |= (uint8_t)(1u << i);
	}

	woz_dfu_set_window_cb(window_changed);

	woz_dwork_init(&s_tick, tick);
	(void)woz_dwork_reschedule(&s_tick, 0);
}
