/**
 * @file ultrawidelock_lat.h
 * Latency tracking for Aliro protocol phases during a walk-up: record BLE_CONNECT as epoch zero,
 * mark timestamps for each phase, emit a report with elapsed intervals and flight-recorder
 * diagnostics.
 */
/*
 * ultrawidelock_lat — walk-up latency trace. One timestamp per phase boundary of the
 * BLE connect -> credential auth -> UWB ranging -> bolt pipeline, stamped at
 * first occurrence and printed as a single consolidated budget line so a bench
 * trace ranks the levers before anything is optimized. Diagnostics only:
 * nothing reads these marks on the protocol path. Compiled out entirely when
 * CONFIG_ULTRAWIDELOCK_LAT_TRACE is disabled (call sites stay unconditional).
 */
#ifndef ULTRAWIDELOCK_LAT_H
#define ULTRAWIDELOCK_LAT_H

#if defined(ESP_PLATFORM)
#include "sdkconfig.h" /* CONFIG_ULTRAWIDELOCK_LAT_TRACE (Zephyr injects autoconf.h itself) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Phase boundaries, in pipeline order. Deltas in the budget line are relative
 * to ULTRAWIDELOCK_LAT_BLE_CONNECT (the walk-up's t=0). */
enum ultrawidelock_lat_phase {
	ULTRAWIDELOCK_LAT_BLE_CONNECT = 0, /* GAP connect on the reader's advertisement */
	ULTRAWIDELOCK_LAT_GATT_SPSM_READ,  /* phone read the SPSM/versions/features characteristic */
	ULTRAWIDELOCK_LAT_GATT_VER_WRITE,  /* phone wrote its selected protocol version */
	ULTRAWIDELOCK_LAT_L2CAP_OPEN,      /* L2CAP CoC connected on the Aliro SPSM */
	ULTRAWIDELOCK_LAT_OP05_RX,         /* phone's Initiate-Access-Protocol received */
	ULTRAWIDELOCK_LAT_AUTH0_TX,        /* AUTH0 command sent */
	ULTRAWIDELOCK_LAT_AUTH0_RSP,       /* AUTH0Response received (fast/standard fork) */
	ULTRAWIDELOCK_LAT_AUTH1_DONE,      /* AUTH1Response verified; EXCHANGE sent */
	ULTRAWIDELOCK_LAT_EXCHANGE_DONE,   /* EXCHANGE response accepted (URSK armed) */
	ULTRAWIDELOCK_LAT_AP_COMPLETED,    /* Reader-Status-AP-Completed sent */
	ULTRAWIDELOCK_LAT_IRS_RX,          /* device's Initiate-Ranging-Session received */
	ULTRAWIDELOCK_LAT_M1_TX,           /* M1 sent (engine's 1st ranging-setup TX) */
	ULTRAWIDELOCK_LAT_M2_RX,           /* M2 received (2nd ranging-setup RX) */
	ULTRAWIDELOCK_LAT_M3_TX,           /* M3 sent (engine's 2nd ranging-setup TX) */
	ULTRAWIDELOCK_LAT_M4_RX,           /* M4 received (3rd ranging-setup RX) */
	ULTRAWIDELOCK_LAT_M4_DONE,         /* UWB session ACTIVE (M4 handled, responder up) */
	ULTRAWIDELOCK_LAT_FIRST_RANGE,     /* first DS-TWR range latched */
	ULTRAWIDELOCK_LAT_TRUSTED_RANGE,   /* layer-4 consensus reached */
	ULTRAWIDELOCK_LAT_NEAR_DWELL,      /* approach threshold met; unlock scheduled */
	ULTRAWIDELOCK_LAT_BOLT_DRIVEN,     /* lock manager drove the unlock (Matter task) */
	ULTRAWIDELOCK_LAT_PHASE_COUNT
};

#if defined(CONFIG_ULTRAWIDELOCK_LAT_TRACE)

/* Start a fresh walk-up trace: clear every mark and stamp BLE_CONNECT. */
void ultrawidelock_lat_begin(void);

/* Stamp a phase at its first occurrence this walk-up; later calls are no-ops.
 * Returns nonzero when this call stamped the phase, 0 otherwise. Cheap (one
 * uptime read + store) — safe on the BLE-host and UWB RX paths. */
int ultrawidelock_lat_mark(enum ultrawidelock_lat_phase phase);

/* Print the consolidated budget line (one printf; call off the protocol path). */
void ultrawidelock_lat_report(void);

#else /* !CONFIG_ULTRAWIDELOCK_LAT_TRACE */

/**
 * Initialize latency tracking; record the BLE_CONNECT epoch as the zero reference for all
 * subsequent phase timestamps.
 */
static inline void ultrawidelock_lat_begin(void)
{
}
/**
 * Record the current uptime for a given Aliro protocol phase if not yet marked; return 1 if newly
 * recorded, 0 if already marked or out of range.
 */
static inline int ultrawidelock_lat_mark(enum ultrawidelock_lat_phase phase)
{
	(void)phase;
	return 0;
}
/**
 * Emit a latency report showing all recorded phase timestamps and elapsed intervals; dump
 * flight-recorder lab traces if CONFIG_ULTRAWIDELOCK_CRED_LAB is enabled and tracing is active.
 */
static inline void ultrawidelock_lat_report(void)
{
}

#endif /* CONFIG_ULTRAWIDELOCK_LAT_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* ULTRAWIDELOCK_LAT_H */
