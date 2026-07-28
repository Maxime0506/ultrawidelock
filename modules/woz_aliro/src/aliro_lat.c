// Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_lat — see aliro_lat.h. Marks come from three tasks (BLE host, UWB RX,
 * reader/unlock); each phase is written once per walk-up and only read by the
 * report after the bolt is driven, so plain stores are sufficient for a
 * diagnostic trace.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "woz_port.h"
#include "woz_log.h"

#include "aliro_lab.h"
#include "aliro_lat.h"

#ifdef CONFIG_ALIRO_LAT_TRACE

/* 0 = unmarked this walk-up (woz_uptime_us() is nonzero by the time BLE is up). */
static int64_t s_stamp_us[ALIRO_LAT_PHASE_COUNT];

static const char *const k_phase_name[] = {
	"connect", "spsm", "ver", "l2cap", "op05", "auth0", "a0rsp", "auth1",   "exch", "apc",
	"irs",     "m1",   "m2",  "m3",    "m4rx", "m4",    "range", "trusted", "near", "bolt",
};
_Static_assert(sizeof(k_phase_name) / sizeof(k_phase_name[0]) == ALIRO_LAT_PHASE_COUNT,
	       "k_phase_name must cover every aliro_lat_phase");

#if defined(CONFIG_WOZ_ALIRO_LAB)
/* aliro_lab (see aliro_lab.h): runtime gate (OFF at boot; `lab on` flips it) and
 * the once-per-walk-up guard for the ph.* dump. */
static bool s_lab_on;
static bool s_lab_dumped;
#endif

/**
 * Initialize latency tracking and record the BLE connection epoch; reset all phase timestamps to
 * zero and clear the lab-dump flag if CONFIG_WOZ_ALIRO_LAB is enabled.
 */
void aliro_lat_begin(void)
{
	memset(s_stamp_us, 0, sizeof(s_stamp_us));
	s_stamp_us[ALIRO_LAT_BLE_CONNECT] = woz_uptime_us();
#if defined(CONFIG_WOZ_ALIRO_LAB)
	s_lab_dumped = false;
#endif
}

/**
 * Record the current uptime for a given Aliro protocol phase if not yet marked; return 1 if newly
 * recorded, 0 if already marked or phase index is out of range.
 */
int aliro_lat_mark(enum aliro_lat_phase phase)
{
	if ((unsigned)phase >= ALIRO_LAT_PHASE_COUNT || s_stamp_us[phase] != 0) {
		return 0;
	}
	s_stamp_us[phase] = woz_uptime_us();
	return 1;
}

/**
 * Print a one-line latency summary to stdout showing each Aliro phase as milliseconds offset from
 * BLE connect, or "-" if the phase was never reached; also emit any flight-recorder lab traces if
 * enabled.
 */
void aliro_lat_report(void)
{
	int64_t t0 = s_stamp_us[ALIRO_LAT_BLE_CONNECT];

	if (t0 == 0) {
		woz_printf("aliro-lat: no trace (no BLE connect marked)\n");
		return;
	}

	/* One line, every phase as +ms from connect ("-" = never reached). */
	woz_printf("aliro-lat:");
	for (int i = 0; i < ALIRO_LAT_PHASE_COUNT; i++) {
		if (s_stamp_us[i] != 0) {
			woz_printf(" %s+%d", k_phase_name[i], (int)((s_stamp_us[i] - t0) / 1000));
		} else {
			woz_printf(" %s-", k_phase_name[i]);
		}
	}
	woz_printf(" ms\n");
	aliro_lab_dump();
}

/* aliro_lab emitters live inside the CONFIG_ALIRO_LAT_TRACE region (the dump
 * reads the stamps above); Kconfig enforces the dependency. */
#if defined(CONFIG_WOZ_ALIRO_LAB)

/**
 * Enable or disable Aliro latency lab tracing (CONFIG_WOZ_ALIRO_LAB flight-recorder output).
 */
void aliro_lab_set_enabled(bool on)
{
	s_lab_on = on;
}

/**
 * Return true if Aliro latency lab tracing (CONFIG_WOZ_ALIRO_LAB) is enabled; false otherwise.
 */
bool aliro_lab_enabled(void)
{
	return s_lab_on;
}

/**
 * Log a timestamped flight-recorder event with description ev to stdout if lab tracing is enabled;
 * no-op if disabled.
 */
void aliro_lab_ev(const char *ev)
{
	if (!s_lab_on) {
		return;
	}
	woz_printf("[ALAB] t=%lld ev=%s\n", (long long)woz_uptime_us(), ev);
}

/**
 * Log a timestamped flight-recorder event with description ev and one key=value pair to stdout if
 * lab tracing is enabled; no-op if disabled.
 */
void aliro_lab_evi(const char *ev, const char *key, long val)
{
	if (!s_lab_on) {
		return;
	}
	woz_printf("[ALAB] t=%lld ev=%s %s=%ld\n", (long long)woz_uptime_us(), ev, key, val);
}

/**
 * Log a timestamped flight-recorder event with description ev and two key=value pairs to stdout if
 * lab tracing is enabled; no-op if disabled.
 */
void aliro_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2)
{
	if (!s_lab_on) {
		return;
	}
	woz_printf("[ALAB] t=%lld ev=%s %s=%ld %s=%ld\n", (long long)woz_uptime_us(), ev, k1, v1,
		   k2, v2);
}

/**
 * Emit flight-recorder lab traces for every recorded Aliro latency phase to stdout in [ALAB] format
 * if lab tracing is enabled and has not yet been dumped.
 */
void aliro_lab_dump(void)
{
	if (!s_lab_on || s_lab_dumped) {
		return;
	}
	s_lab_dumped = true;
	for (int i = 0; i < ALIRO_LAT_PHASE_COUNT; i++) {
		if (s_stamp_us[i] != 0) {
			woz_printf("[ALAB] t=%lld ev=ph.%s\n", (long long)s_stamp_us[i],
				   k_phase_name[i]);
		}
	}
}

#endif /* CONFIG_WOZ_ALIRO_LAB */

#endif /* CONFIG_ALIRO_LAT_TRACE */
