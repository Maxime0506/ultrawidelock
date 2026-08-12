// Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
// parsed into a scored walk-up report. Ships in every Aliro
// build (CONFIG_ULTRAWIDELOCK_CRED_LAB defaults y, like the sibling uwbdiag trace) but is
// OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
// any firmware profiles on demand with no reflash. Set CONFIG_ULTRAWIDELOCK_CRED_LAB=n to
// strip it from a hardened production image.
/*
 * ultrawidelock_lab — one line per event, `[ALAB] t=<us> ev=<name>[ <key>=<val>]`, with
 * t from woz_uptime_us(). Emit only from the BLE-host or Matter task — never
 * from the UWB RX/ISR path. UWB-side phase boundaries are latched by
 * ultrawidelock_lat_mark() (a plain store) and printed later by ultrawidelock_lab_dump(), which
 * runs once per walk-up from ultrawidelock_lat_report() (bolt) or the disconnect path,
 * whichever comes first. Implemented in ultrawidelock_lat.c.
 */
#ifndef ULTRAWIDELOCK_LAB_H
#define ULTRAWIDELOCK_LAB_H

#include <stdbool.h>

#if defined(ESP_PLATFORM)
#include "sdkconfig.h" /* CONFIG_ULTRAWIDELOCK_CRED_LAB (Zephyr injects autoconf.h itself) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_ULTRAWIDELOCK_CRED_LAB)

/* Runtime gate: emitters are silent until enabled. OFF at boot; the console
 * `lab on`/`lab off` command drives this so a lab-flashed image traces on
 * demand. Set from the console task; a plain store is enough for a diagnostic. */
void ultrawidelock_lab_set_enabled(bool on);
bool ultrawidelock_lab_enabled(void);

/* One trace line, stamped now (no-op while disabled). */
void ultrawidelock_lab_ev(const char *ev);

/* One trace line with a single integer attribute, stamped now (no-op while
 * disabled). */
void ultrawidelock_lab_evi(const char *ev, const char *key, long val);

/* Same, with two integer attributes. Used where one number cannot identify the
 * thing traced: a ranging SDU needs both its protocol and its message id, since
 * the ids repeat across protocols (proto-2 id-1 is Initiate-Ranging-Session,
 * proto-1 id-1 is M2). */
void ultrawidelock_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2);

/* Print a `ph.<name>` line for every latency phase stamped this walk-up, at the
 * phase's own timestamp (no-op while disabled). One-shot until ultrawidelock_lat_begin()
 * opens the next walk-up; call off the UWB path. */
void ultrawidelock_lab_dump(void);

#else

/**
 * Enable or disable Aliro Lab instrumentation. When disabled, all recording functions become
 * no-ops. Caller must invoke before any approach transactions if instrumentation is desired.
 */
static inline void ultrawidelock_lab_set_enabled(bool on)
{
	(void)on;
}

/**
 * Return whether Aliro Lab instrumentation is active. Returns false when
 * CONFIG_ULTRAWIDELOCK_CRED_LAB is not enabled.
 */
static inline bool ultrawidelock_lab_enabled(void)
{
	return false;
}

/**
 * Record a named event for Aliro Lab when enabled; no-op stub when disabled. Caller passes a unique
 * event name; the call is recorded with a timestamp.
 */
static inline void ultrawidelock_lab_ev(const char *ev)
{
	(void)ev;
}

/**
 * Record a named event with one integer sample for Aliro Lab when enabled; no-op stub when
 * disabled. Caller passes event name, key (e.g., "distance_cm"), and value; the triple is recorded
 * with a timestamp.
 */
static inline void ultrawidelock_lab_evi(const char *ev, const char *key, long val)
{
	(void)ev;
	(void)key;
	(void)val;
}

/**
 * Record a named event with two integer samples for Aliro Lab when enabled; no-op stub when
 * disabled. Caller passes event name, two key-value pairs; both are recorded with a timestamp.
 */
static inline void ultrawidelock_lab_evi2(const char *ev, const char *k1, long v1, const char *k2,
					  long v2)
{
	(void)ev;
	(void)k1;
	(void)v1;
	(void)k2;
	(void)v2;
}

/**
 * No-op stub when CONFIG_ULTRAWIDELOCK_CRED_LAB is not enabled. Caller may invoke any time; does
 * nothing.
 */
static inline void ultrawidelock_lab_dump(void)
{
}

#endif /* CONFIG_ULTRAWIDELOCK_CRED_LAB */

#ifdef __cplusplus
}
#endif

#endif /* ULTRAWIDELOCK_LAB_H */
