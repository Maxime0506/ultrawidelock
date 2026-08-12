// Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.
/*
 * ultrawidelock_lat — see ultrawidelock_lat.h. Marks come from three tasks (BLE host, UWB RX,
 * reader/unlock); each phase is written once per walk-up and only read by the
 * report after the bolt is driven, so plain stores are sufficient for a
 * diagnostic trace.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ultrawidelock_port.h"
#include "ultrawidelock_log.h"

#include "ultrawidelock_lab.h"
#include "ultrawidelock_lat.h"

#ifdef CONFIG_ULTRAWIDELOCK_LAT_TRACE

/* 0 = unmarked this walk-up (ultrawidelock_uptime_us() is nonzero by the time BLE is up). */
static int64_t s_stamp_us[ULTRAWIDELOCK_LAT_PHASE_COUNT];

static const char *const k_phase_name[] = {
	"connect", "spsm", "ver", "l2cap", "op05", "auth0", "a0rsp", "auth1",   "exch", "apc",
	"irs",     "m1",   "m2",  "m3",    "m4rx", "m4",    "range", "trusted", "near", "bolt",
};
_Static_assert(sizeof(k_phase_name) / sizeof(k_phase_name[0]) == ULTRAWIDELOCK_LAT_PHASE_COUNT,
	       "k_phase_name must cover every ultrawidelock_lat_phase");

#if defined(CONFIG_ULTRAWIDELOCK_CRED_LAB)
/* ultrawidelock_lab (see ultrawidelock_lab.h): runtime gate (OFF at boot; `lab on` flips it) and
 * the once-per-walk-up guard for the ph.* dump. */
static bool s_lab_on;
static bool s_lab_dumped;
#endif

/**
 * Initialize latency tracking and record the BLE connection epoch; reset all phase timestamps to
 * zero and clear the lab-dump flag if CONFIG_ULTRAWIDELOCK_CRED_LAB is enabled.
 */
void ultrawidelock_lat_begin(void)
{
	memset(s_stamp_us, 0, sizeof(s_stamp_us));
	s_stamp_us[ULTRAWIDELOCK_LAT_BLE_CONNECT] = ultrawidelock_uptime_us();
#if defined(CONFIG_ULTRAWIDELOCK_CRED_LAB)
	s_lab_dumped = false;
#endif
}

/**
 * Record the current uptime for a given credential protocol phase if not yet marked; return 1 if
 * newly recorded, 0 if already marked or phase index is out of range.
 */
int ultrawidelock_lat_mark(enum ultrawidelock_lat_phase phase)
{
	if ((unsigned)phase >= ULTRAWIDELOCK_LAT_PHASE_COUNT || s_stamp_us[phase] != 0) {
		return 0;
	}
	s_stamp_us[phase] = ultrawidelock_uptime_us();
	return 1;
}

/**
 * Print a one-line latency summary to stdout showing each credential phase as milliseconds offset
 * from BLE connect, or "-" if the phase was never reached; also emit any flight-recorder lab traces
 * if enabled.
 */
void ultrawidelock_lat_report(void)
{
	int64_t t0 = s_stamp_us[ULTRAWIDELOCK_LAT_BLE_CONNECT];

	if (t0 == 0) {
		ultrawidelock_printf("ultrawidelock-lat: no trace (no BLE connect marked)\n");
		return;
	}

	/* One line, every phase as +ms from connect ("-" = never reached). */
	ultrawidelock_printf("ultrawidelock-lat:");
	for (int i = 0; i < ULTRAWIDELOCK_LAT_PHASE_COUNT; i++) {
		if (s_stamp_us[i] != 0) {
			ultrawidelock_printf(" %s+%d", k_phase_name[i], (int)((s_stamp_us[i] - t0) / 1000));
		} else {
			ultrawidelock_printf(" %s-", k_phase_name[i]);
		}
	}
	ultrawidelock_printf(" ms\n");
	ultrawidelock_lab_dump();
}

/* ultrawidelock_lab emitters live inside the CONFIG_ULTRAWIDELOCK_LAT_TRACE region (the dump
 * reads the stamps above); Kconfig enforces the dependency. */
#if defined(CONFIG_ULTRAWIDELOCK_CRED_LAB)

/**
 * Enable or disable credential latency lab tracing (CONFIG_ULTRAWIDELOCK_CRED_LAB flight-recorder
 * output).
 */
void ultrawidelock_lab_set_enabled(bool on)
{
	s_lab_on = on;
}

/**
 * Return true if credential latency lab tracing (CONFIG_ULTRAWIDELOCK_CRED_LAB) is enabled; false
 * otherwise.
 */
bool ultrawidelock_lab_enabled(void)
{
	return s_lab_on;
}

/**
 * Log a timestamped flight-recorder event with description ev to stdout if lab tracing is enabled;
 * no-op if disabled.
 */
void ultrawidelock_lab_ev(const char *ev)
{
	if (!s_lab_on) {
		return;
	}
	ultrawidelock_printf("[ALAB] t=%lld ev=%s\n", (long long)ultrawidelock_uptime_us(), ev);
}

/**
 * Log a timestamped flight-recorder event with description ev and one key=value pair to stdout if
 * lab tracing is enabled; no-op if disabled.
 */
void ultrawidelock_lab_evi(const char *ev, const char *key, long val)
{
	if (!s_lab_on) {
		return;
	}
	ultrawidelock_printf("[ALAB] t=%lld ev=%s %s=%ld\n", (long long)ultrawidelock_uptime_us(),
			     ev, key, val);
}

/**
 * Log a timestamped flight-recorder event with description ev and two key=value pairs to stdout if
 * lab tracing is enabled; no-op if disabled.
 */
void ultrawidelock_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2)
{
	if (!s_lab_on) {
		return;
	}
	ultrawidelock_printf("[ALAB] t=%lld ev=%s %s=%ld %s=%ld\n",
			     (long long)ultrawidelock_uptime_us(), ev, k1, v1, k2, v2);
}

/**
 * Emit flight-recorder lab traces for every recorded credential latency phase to stdout in [ALAB]
 * format if lab tracing is enabled and has not yet been dumped.
 */
void ultrawidelock_lab_dump(void)
{
	if (!s_lab_on || s_lab_dumped) {
		return;
	}
	s_lab_dumped = true;
	for (int i = 0; i < ULTRAWIDELOCK_LAT_PHASE_COUNT; i++) {
		if (s_stamp_us[i] != 0) {
			ultrawidelock_printf("[ALAB] t=%lld ev=ph.%s\n", (long long)s_stamp_us[i],
				   k_phase_name[i]);
		}
	}
}

#endif /* CONFIG_ULTRAWIDELOCK_CRED_LAB */

#endif /* CONFIG_ULTRAWIDELOCK_LAT_TRACE */
