/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * DWM3001CDK standalone Aliro reader.
 *
 * One board: the nRF52833 runs the BLE peripheral and the Aliro reader engine,
 * and the DW3110 in the same DWM3001C module does the UWB ranging. No host MCU
 * board, no seated DWM3000EVB, no NFC (the CDK has none).
 *
 * Stage 0 keeps main deliberately thin. Its job is to hold the whole call graph
 * live so the linker cannot garbage-collect the engine and hand back a size
 * number that flatters us.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_device.h>

#include "aliro_approach.h"
#include "aliro_prov.h" /* aliro_prov_erase, for the factory-reset button */
#include "aliro_reader.h"
#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
#include "matter_commission.h"
#include "matter_fab_settings.h" /* matter_fab_erase, the Matter half of a reset */
#endif
#include "status_led.h"
#include "woz_uwb_facade.h"

#if IS_ENABLED(CONFIG_WOZ_DFU_RECEIVER)
/* src/dfu_ble_zephyr.c. One function, so it carries no header of its own. */
int dfu_ble_start(void);
#endif

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
#include <mbedtls/memory_buffer_alloc.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
/* Reported at the grant, because by then the unlock has done every P-256 and
 * AES-GCM operation it is going to do. The peak is cumulative since boot, so it
 * covers BLE pairing and the Aliro exchange too, not only the ranging. */
static void heap_peak_log(const char *when)
{
	size_t used = 0;
	size_t blocks = 0;

	mbedtls_memory_buffer_alloc_max_get(&used, &blocks);
	LOG_INF("mbedtls heap peak @%s: %u B of %u (%u blocks)", when,
		(unsigned int)used, (unsigned int)CONFIG_MBEDTLS_HEAP_SIZE,
		(unsigned int)blocks);
}
#endif /* CONFIG_ALIRO_HEAP_PROBE */

/* The per-frame UWB diagnostic trace (DIAGK) defaults ON for nRF targets but OFF
 * for the ESP32, because its printf on every ranging frame blocks the callback
 * long enough to miss the DW3110 delayed-TX slot deadline -- the ESP port disables
 * it for exactly that reason (bench-correlated late RESPONSE arms). What shell this
 * board has exists only in provisioning mode, where the radios never start, so
 * `uwbdiag off` can never be typed at a walk-up; force it off here before any
 * ranging starts. See modules/woz_uwb/src/facade/woz_diag.h. */
extern volatile int woz_uwb_diag_on;

/* Reader status housekeeping: the engine expects a periodic tick to age out a
 * stalled transaction and to drive the ranging power gate's decay. 250 ms is
 * the cadence the ESP32 port runs. */
#define ALIRO_TICK_MS 250

/* How long the ranging LED holds after the last range landed. Four ticks: long
 * enough that no rate iOS ranges at can make it stutter, short enough that a
 * phone put in a pocket drops the light while the person is still in the room. */
#define ALIRO_LED_RANGE_HOLD_MS 1000

/* Wakes the grant loop the moment a range is latched, instead of on the next
 * 250 ms tick.
 *
 * The tick alone quantized every unlock and relock by 0-250 ms on top of the
 * structural ~1 s trust floor, and worse, 250 ms beats against the 192 ms
 * ranging block, so the delay a walk-up got was a lottery rather than a
 * constant. The tick REMAINS as the take timeout: it is still what ages out a
 * stalled transaction and decays the power gate when no range is arriving.
 *
 * Count limit 1 because the loop reads the generation counter, not a queue --
 * two latches between wakes still mean exactly one pass.
 *
 * Declared and initialised separately rather than with K_SEM_DEFINE: semgrep's
 * C parser cannot read that macro at file scope, and the security gate treats
 * an unparseable file as zero rule coverage rather than a clean result, which
 * would silently drop every rule that guards this file. */
static struct k_sem s_range_sig;

/**
 * Wake the grant loop on an accepted range latch. Runs on the UWB RX path, so it does nothing but
 * give the semaphore -- the float math in the approach controller stays on the main thread.
 */
static void on_range_latched(void)
{
	k_sem_give(&s_range_sig);
}

/* Provisioning mode: hold SW2 (the board's sw0 alias, P0.02) through reset.
 *
 * The reader identity is per-device data in the settings store, never a string
 * in the image, so it has to arrive at runtime. This board's only input path is
 * the USB device port wired straight to the nRF52833 -- RTT is output-only --
 * so provisioning mode brings up CDC-ACM and the `aliro` console on it.
 *
 * The radios stay down in this mode on purpose. It keeps USB's millisecond SOF
 * interrupts away from the DW3110's delayed-TX reply window (the timing that
 * commit 5b8d06b had to fight for on this single-core part), and it means the
 * console can never be reached while a walk-up is in flight. */
#if IS_ENABLED(CONFIG_ALIRO_PROV_CONSOLE)
/**
 * Check GPIO SW0 (active-low, pulled up in DTS) to see if provisioning is requested at boot.
 * Returns true if SW0 is ready and held (logical 1), false otherwise.
 */
static bool provisioning_requested(void)
{
	static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

	if (!gpio_is_ready_dt(&sw)) {
		return false;
	}
	if (gpio_pin_configure_dt(&sw, GPIO_INPUT) != 0) {
		return false;
	}
	/* Active low with a pull-up in the board DTS; _dt() returns logical level. */
	return gpio_pin_get_dt(&sw) == 1;
}

/* Runs the console and nothing else. Never returns: leaving this function would
 * start the radios in a mode the user did not ask for. */
static void provisioning_mode(void)
{
	int rc = usb_enable(NULL);

	/* Solid blue for as long as this mode lasts. Provisioning looks exactly
	 * like a hung boot from the outside otherwise -- the radios are down, so
	 * nothing else on the board moves. */
	status_led_signal(STATUS_LED_PROV_MODE, true);

	if (rc != 0) {
		/* Nothing to fall back to: without USB there is no input path at
		 * all on this board, so say so on RTT and stop. */
		LOG_ERR("provisioning mode: usb_enable rc=%d, no console available", rc);
	} else {
		LOG_INF("provisioning mode: USB console up, radios down");
	}

	while (1) {
		k_msleep(1000);
	}
}
#endif /* CONFIG_ALIRO_PROV_CONSOLE */

/* Factory reset: hold SW2 (the sw0 alias, P0.02) through reset.
 *
 * WITHOUT THIS THE BOARD IS A BRICK AFTER A FAILED PAIRING, which is not a
 * bench annoyance but the ordinary failure. A commissioning that gets far
 * enough to install a fabric and then times out leaves the fabric stored; the
 * advert gate then offers Aliro 0xFFF2 instead of commissionable; the
 * controller can neither discover the node nor open a commissioning window on
 * an accessory it has already forgotten. On 2026-08-02 that state was reached
 * four times in one evening and cleared four times with a debugger. A user has
 * no debugger.
 *
 * Held through reset rather than long-pressed while running: it matches the
 * provisioning console's idiom on this same button, needs no timer, no
 * debounce and no thread on a part at 96% RAM, and cannot fire while a walk-up
 * is in flight.
 *
 * The Thread credentials are deliberately NOT erased -- see aliro_prov_erase().
 */
#if IS_ENABLED(CONFIG_ALIRO_FACTORY_RESET_BUTTON)
/**
 * Check GPIO SW0 (active-low, pulled up in DTS) at boot. If SW0 is held (logical 1), blink the lock
 * LED as user feedback, erase Aliro provisioning and Matter fabric (if CONFIG_ALIRO_MATTER_BLE is
 * on), and log that the board is now commissionable on the next boot. Returns silently if GPIO is
 * not ready or if SW0 is not held.
 */
static void factory_reset_if_requested(void)
{
	static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

	if (!gpio_is_ready_dt(&sw) || gpio_pin_configure_dt(&sw, GPIO_INPUT) != 0) {
		return;
	}
	/* Active low with a pull-up in the board DTS; _dt() returns logical level. */
	if (gpio_pin_get_dt(&sw) != 1) {
		return;
	}

	LOG_WRN("SW2 held at boot: FACTORY RESET");
	/*
	 * The only feedback this board can give someone without a debugger. RTT
	 * says it too, but a user holding a button needs to see that the hold
	 * was long enough and registered. Blocking, and it goes through
	 * status_led.c because that owns the pin from SYS_INIT onwards -- driving
	 * it from here as well would put the blink and the heartbeat on the same
	 * lamp, 120 ms apart.
	 */
	status_led_boot_blink();

	(void)aliro_prov_erase();
#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
	(void)matter_fab_erase();
#endif
	LOG_WRN("factory reset done; commissionable on the next boot");
}
#endif

/**
 * Entry point for the DWM3001CDK reader application. Initializes provisioning and factory-reset
 * paths, starts the Aliro BLE reader and optional Matter commissioning and DFU receiver, then runs
 * the approach controller loop. Feeds the controller trusted ranges on each new latch generation
 * and observes untrusted ranges for departure detection. Grants unlock on approach prediction or
 * threshold crossing, relocks on departure or abort, and exits with an error code if reader startup
 * fails.
 */
int main(void)
{
	/* Off before the radio comes up: keeps the ranging callbacks print-free so the
	 * delayed RESPONSE/FINAL TX can hit its microsecond turnaround. */
	woz_uwb_diag_on = 0;

	/* ASCII only: the console is a byte stream, and a UTF-8 dash renders as
	 * mojibake in RTT Viewer. */
	LOG_INF("openaliro reader: DWM3001CDK (nRF52833 + DW3110)");

#if IS_ENABLED(CONFIG_ALIRO_PROV_CONSOLE)
	if (provisioning_requested()) {
		provisioning_mode(); /* never returns */
	}
#endif

#if IS_ENABLED(CONFIG_ALIRO_FACTORY_RESET_BUTTON)
	factory_reset_if_requested();
#endif

	int rc = aliro_reader_start();

	if (rc != 0) {
		LOG_ERR("aliro_reader_start rc=%d", rc);
		/* main() is about to return and this board has no console, so a
		 * solid D12 is the only account anyone gets of why it does
		 * nothing. The LED tick lives on the system work queue and
		 * outlives this thread, so the light stays on. */
		status_led_signal(STATUS_LED_FAULT, true);
		return rc;
	}

#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
	/* After the reader, because the reader owns BLE and the advertising set;
	 * this only attaches handlers to the 0xFFF6 transport that SYS_INIT
	 * already brought up. */
	(void)matter_commission_init();
#endif

#if IS_ENABLED(CONFIG_WOZ_DFU_RECEIVER)
	/* Also after the reader, and for the same reason: registering an L2CAP
	 * PSM needs the host up, and the reader is what enables it. The channel
	 * refuses every connection until SW2 opens a window, so registering it
	 * here costs nothing an idle board can be reached through. */
	(void)dfu_ble_start();
#endif

	/* Bridge the trusted UWB range stream to the Wallet grant.
	 *
	 * aliro_reader_start brings up BLE and the CCC/FiRa ranging engine, and the engine
	 * latches a trust-gated distance (fira_session) on every good block -- but nothing
	 * consumes it on its own. The shipped Matter lock wires this in its app_main
	 * (ports/esp32/apps/matter-lock): trusted range -> approach controller -> on UNLOCK,
	 * aliro_reader_notify_unlock(true), which sends Reader Status = Unsecured and animates
	 * the phone. The standalone reader has to do the same or a perfectly good range never
	 * becomes an unlock. There is no bolt on this board: the grant IS the product. */
	struct aliro_approach approach;

	aliro_approach_init(&approach, NULL); /* factory defaults: unlock 100 cm, relock 250 cm */

	/* Same seam the ESP32 matter-lock uses (app_main.cpp on_uwb_range): the engine
	 * signals, this thread decides. Both lines run before the listener can fire --
	 * the semaphore has to exist before anything is allowed to give it, and the
	 * controller has to be initialised before a signal can reach it. */
	k_sem_init(&s_range_sig, 0, 1);
	woz_uwb_set_range_listener(on_range_latched);

	uint32_t last_gen = woz_uwb_range_generation();
	/* The last range OBSERVED for departure, trusted or not; see the loop. */
	uint32_t last_obs_gen = last_gen;
	/* A third epoch, for the activity LED alone. The two above are consumed
	 * at different moments on purpose -- that is what keeps a late-trusted
	 * latch from being counted twice and what stops the silence clock being
	 * refreshed -- so folding a light into either would change when a relock
	 * fires. This one is read-only with respect to the unlock logic. */
	uint32_t led_gen = last_gen;
	int64_t led_range_ms = 0;
	bool present = false;
	bool granted = false;

	while (1) {
		int64_t now = k_uptime_get();
		uint32_t gen = woz_uwb_range_generation();
		int32_t cm = 0;
		enum aliro_approach_action act;

		aliro_reader_status_tick(now);

		/*
		 * D11: what the phone is doing. The loop wakes on the latch
		 * itself, so this lights within a round rather than on the next
		 * 250 ms tick. Any range counts, trusted or not -- the light
		 * says "the radios are working on someone", which is the
		 * question being asked when you are standing in front of a
		 * board that has not opened.
		 * ALIRO_LED_RANGE_HOLD_MS outlives one round at every rate iOS
		 * uses, so a walk-up shows as a steady 4 Hz rather than a
		 * stutter, and it drops back to the 1 Hz session light about a
		 * second after the phone stops ranging (which a still phone
		 * does, with the session still up).
		 */
		if (gen != led_gen) {
			led_gen = gen;
			led_range_ms = now;
		}
		/* The != 0 is not redundant: uptime is a few tens of ms when this
		 * loop first runs, so without it every board reports ranging for
		 * its first second. */
		bool ranging = led_range_ms != 0 &&
			       (now - led_range_ms) < ALIRO_LED_RANGE_HOLD_MS;

		status_led_signal(STATUS_LED_RANGING, ranging);
		status_led_signal(STATUS_LED_SESSION, aliro_reader_session_active());
#if IS_ENABLED(CONFIG_ALIRO_MATTER_BLE)
		/* D12: an uncommissioned node cannot unlock anything, and it is
		 * indistinguishable from a working one until someone walks up. */
		status_led_signal(STATUS_LED_UNCOMMISSIONED, !matter_commission_has_fabric());
#endif

		/* Feed exactly one sample per NEWLY accepted trusted range (the generation epoch
		 * advances only on an accepted latch), mirroring the ESP lock's per-wake feed. A
		 * stale latch -- iOS stops ranging once the phone holds still -- keeps the old
		 * generation, so it drives a tick, not a fresh approach sample. */
		if (gen != last_gen && woz_uwb_trusted_range_cm(&cm)) {
			last_gen = gen;
			last_obs_gen = gen;
			present = true;
			act = aliro_approach_feed(&approach, now, cm);
		} else {
			/*
			 * A fresh range the integrity consensus will not vouch
			 * for still says something -- about DEPARTURE only. Far
			 * ranges are the ones it declines, so without this the
			 * walk-away relock can never fire; see
			 * aliro_approach_observe_departure() for why reading an
			 * unvouched range is safe in that one direction.
			 *
			 * last_gen is deliberately NOT consumed here. Trust can
			 * arrive late for a latch already taken (the good-run
			 * counter builds across blocks), and the retry above is
			 * what catches it. A separate epoch keeps this from
			 * observing the same range twice, which would refresh
			 * the silence clock and stop it ever expiring.
			 */
			if (gen != last_obs_gen) {
				int32_t raw = 0;

				last_obs_gen = gen;
				if (woz_uwb_last_range_cm(&raw)) {
					aliro_approach_observe_departure(&approach, now, raw);
				}
			}
			act = aliro_approach_tick(&approach, now);
		}

		switch (act) {
		case ALIRO_APPROACH_UNLOCK_PREDICT:
		case ALIRO_APPROACH_UNLOCK_THRESHOLD:
			aliro_reader_notify_unlock(true); /* Reader Status -> Unsecured (animate) */
			status_led_signal(STATUS_LED_UNLOCKED, true);
			granted = true;
#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
			heap_peak_log("unlock");
#endif
			break;
		case ALIRO_APPROACH_RELOCK_DEPART:
		case ALIRO_APPROACH_RELOCK_ABORT:
			aliro_reader_notify_unlock(false); /* Reader Status -> Secured */
			status_led_signal(STATUS_LED_UNLOCKED, false);
			granted = false;
			break;
		default:
			break;
		}

		/* Departure: the peer's Aliro session ended (walked away / phone pocketed). iOS
		 * ranging silence alone does NOT mean departed (a still phone stops ranging too),
		 * so gate on the session, not on range age. Tell Wallet Secured once and reset. */
		if (present && !aliro_reader_session_active()) {
			/*
			 * Reaching here with the bolt still open means the silence
			 * relock in aliro_approach_tick() did NOT fire, and this is
			 * the only moment that proves it: the Secured is about to go
			 * out with no session left to carry it.
			 *
			 * last_cm is what the controller was actually FED, which is
			 * not what the status line prints. main.c feeds only a range
			 * woz_uwb_trusted_range_cm() vouches for; the trace prints
			 * the raw latch either way, so a walk-away can show 390 cm
			 * on screen while the controller last saw 199 cm. Printing
			 * both the value and its age says which of the two gates
			 * held. MUST run before aliro_approach_gone(), which
			 * re-inits the struct and erases the evidence.
			 */
			if (granted) {
				LOG_WRN("departure fallback: last FED %d cm, %u ms ago "
					"(gate: >= %d cm held for %d ms)",
					approach.last_cm,
					approach.last_feed_ms != 0
						? (unsigned int)(now - approach.last_feed_ms)
						: 0u,
					approach.cfg.relock_cm, approach.cfg.far_silence_ms);
			}
			(void)aliro_approach_gone(&approach);
			if (granted) {
				aliro_reader_notify_unlock(false);
				status_led_signal(STATUS_LED_UNLOCKED, false);
				granted = false;
			}
			present = false;
		}

		/* Wake on the next latch, or on the housekeeping tick if none comes.
		 * A latch that lands while this pass is still running leaves the
		 * semaphore given, so the take returns at once and no range waits. */
		(void)k_sem_take(&s_range_sig, K_MSEC(ALIRO_TICK_MS));
	}
	return 0;
}
