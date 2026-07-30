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

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "aliro_approach.h"
#include "aliro_prov.h"
#include "aliro_reader.h"
#include "woz_uwb_facade.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* The per-frame UWB diagnostic trace (DIAGK) defaults ON for nRF targets but OFF
 * for the ESP32, because its printf on every ranging frame blocks the callback
 * long enough to miss the DW3110 delayed-TX slot deadline -- the ESP port disables
 * it for exactly that reason (bench-correlated late RESPONSE arms). This board has
 * no shell (CONFIG_SHELL=n) to run `uwbdiag off`, so force it off here before any
 * ranging starts. See modules/woz_uwb/src/facade/woz_diag.h. */
extern volatile int woz_uwb_diag_on;

/* Reader status housekeeping: the engine expects a periodic tick to age out a
 * stalled transaction and to drive the ranging power gate's decay. 250 ms is
 * the cadence the ESP32 port runs. */
#define ALIRO_TICK_MS 250

/* Adopts the reader identity baked into the image by CONFIG_ALIRO_PROV_SEED_HEX.
 *
 * This board cannot be commissioned into Apple Home on its own, so the only way
 * it holds an Apple-issued Aliro credential is to adopt one exported from a board
 * that was. Import persists to the settings store and commits in memory, so it
 * has to run before aliro_reader_start reads the identity to build the
 * advertisement. Returns 0 when nothing was seeded or the seed took. */
static int seed_provisioning(void)
{
	static uint8_t blob[ALIRO_PROV_BLOB_MAX];
	const char *hex = CONFIG_ALIRO_PROV_SEED_HEX;
	size_t hex_len = strlen(hex);

	if (hex_len == 0u) {
		return 0;
	}

	size_t len = hex2bin(hex, hex_len, blob, sizeof(blob));

	if (len == 0u) {
		LOG_ERR("prov seed: %u hex chars did not decode (odd length, "
			"non-hex, or over the %u-byte blob cap)",
			(unsigned int)hex_len, (unsigned int)sizeof(blob));
		return -EINVAL;
	}

	int rc = aliro_reader_import_blob(blob, len);

	if (rc != 0) {
		/* -1 malformed blob, -2 settings write failed. */
		LOG_ERR("prov seed: import of %u bytes rc=%d", (unsigned int)len, rc);
		return rc;
	}
	LOG_INF("prov seed: adopted a %u-byte cloned identity", (unsigned int)len);
	return 0;
}

int main(void)
{
	/* Off before the radio comes up: keeps the ranging callbacks print-free so the
	 * delayed RESPONSE/FINAL TX can hit its microsecond turnaround. */
	woz_uwb_diag_on = 0;

	/* ASCII only: the console is a byte stream, and a UTF-8 dash renders as
	 * mojibake in RTT Viewer. */
	LOG_INF("openaliro reader: DWM3001CDK (nRF52833 + DW3110)");

	/* Deliberately not fatal: a bad seed leaves the dev identity in place, and a
	 * board that advertises unresolvably is far easier to diagnose over RTT than
	 * one that never boots. */
	(void)seed_provisioning();

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
