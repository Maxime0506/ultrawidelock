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
#include "aliro_reader.h"
#include "woz_uwb_facade.h"

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

	int rc = aliro_reader_start();

	if (rc != 0) {
		LOG_ERR("aliro_reader_start rc=%d", rc);
		return rc;
	}

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

	uint32_t last_gen = woz_uwb_range_generation();
	bool present = false;
	bool granted = false;

	while (1) {
		int64_t now = k_uptime_get();
		uint32_t gen = woz_uwb_range_generation();
		int32_t cm = 0;
		enum aliro_approach_action act;

		aliro_reader_status_tick(now);

		/* Feed exactly one sample per NEWLY accepted trusted range (the generation epoch
		 * advances only on an accepted latch), mirroring the ESP lock's per-wake feed. A
		 * stale latch -- iOS stops ranging once the phone holds still -- keeps the old
		 * generation, so it drives a tick, not a fresh approach sample. */
		if (gen != last_gen && woz_uwb_trusted_range_cm(&cm)) {
			last_gen = gen;
			present = true;
			act = aliro_approach_feed(&approach, now, cm);
		} else {
			act = aliro_approach_tick(&approach, now);
		}

		switch (act) {
		case ALIRO_APPROACH_UNLOCK_PREDICT:
		case ALIRO_APPROACH_UNLOCK_THRESHOLD:
			aliro_reader_notify_unlock(true); /* Reader Status -> Unsecured (animate) */
			granted = true;
#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
			heap_peak_log("unlock");
#endif
			break;
		case ALIRO_APPROACH_RELOCK_DEPART:
		case ALIRO_APPROACH_RELOCK_ABORT:
			aliro_reader_notify_unlock(false); /* Reader Status -> Secured */
			granted = false;
			break;
		default:
			break;
		}

		/* Departure: the peer's Aliro session ended (walked away / phone pocketed). iOS
		 * ranging silence alone does NOT mean departed (a still phone stops ranging too),
		 * so gate on the session, not on range age. Tell Wallet Secured once and reset. */
		if (present && !aliro_reader_session_active()) {
			(void)aliro_approach_gone(&approach);
			if (granted) {
				aliro_reader_notify_unlock(false);
				granted = false;
			}
			present = false;
		}

		k_msleep(ALIRO_TICK_MS);
	}
	return 0;
}
