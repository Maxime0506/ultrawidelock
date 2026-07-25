// Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
// parsed by tools/aliro_lab.py into a scored walk-up report. Ships in every Aliro
// build (CONFIG_WOZ_ALIRO_LAB defaults y, like the sibling uwbdiag trace) but is
// OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
// any firmware profiles on demand with no reflash. Set CONFIG_WOZ_ALIRO_LAB=n to
// strip it from a hardened production image.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_lab — one line per event, `[ALAB] t=<us> ev=<name>[ <key>=<val>]`, with
 * t from woz_uptime_us(). Emit only from the BLE-host or Matter task — never
 * from the UWB RX/ISR path. UWB-side phase boundaries are latched by
 * aliro_lat_mark() (a plain store) and printed later by aliro_lab_dump(), which
 * runs once per walk-up from aliro_lat_report() (bolt) or the disconnect path,
 * whichever comes first. Implemented in aliro_lat.c.
 */
#ifndef ALIRO_LAB_H
#define ALIRO_LAB_H

#include <stdbool.h>

#if defined(ESP_PLATFORM)
#include "sdkconfig.h" /* CONFIG_WOZ_ALIRO_LAB (Zephyr injects autoconf.h itself) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_WOZ_ALIRO_LAB)

/* Runtime gate: emitters are silent until enabled. OFF at boot; the console
 * `lab on`/`lab off` command drives this so a lab-flashed image traces on
 * demand. Set from the console task; a plain store is enough for a diagnostic. */
void aliro_lab_set_enabled(bool on);
bool aliro_lab_enabled(void);

/* One trace line, stamped now (no-op while disabled). */
void aliro_lab_ev(const char *ev);

/* One trace line with a single integer attribute, stamped now (no-op while
 * disabled). */
void aliro_lab_evi(const char *ev, const char *key, long val);

/* Same, with two integer attributes. Used where one number cannot identify the
 * thing traced: a ranging SDU needs both its protocol and its message id, since
 * the ids repeat across protocols (proto-2 id-1 is Initiate-Ranging-Session,
 * proto-1 id-1 is M2). */
void aliro_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2);

/* Print a `ph.<name>` line for every latency phase stamped this walk-up, at the
 * phase's own timestamp (no-op while disabled). One-shot until aliro_lat_begin()
 * opens the next walk-up; call off the UWB path. */
void aliro_lab_dump(void);

#else

static inline void aliro_lab_set_enabled(bool on)
{
	(void)on;
}

static inline bool aliro_lab_enabled(void)
{
	return false;
}

static inline void aliro_lab_ev(const char *ev)
{
	(void)ev;
}

static inline void aliro_lab_evi(const char *ev, const char *key, long val)
{
	(void)ev;
	(void)key;
	(void)val;
}

static inline void aliro_lab_evi2(const char *ev, const char *k1, long v1, const char *k2, long v2)
{
	(void)ev;
	(void)k1;
	(void)v1;
	(void)k2;
	(void)v2;
}

static inline void aliro_lab_dump(void)
{
}

#endif /* CONFIG_WOZ_ALIRO_LAB */

#ifdef __cplusplus
}
#endif

#endif /* ALIRO_LAB_H */
