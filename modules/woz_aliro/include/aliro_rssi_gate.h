// BLE-RSSI ranging power gate: decides when the phone is close enough that arming
// UWB ranging is worth the radio's RX power. Pure sample-in/state-out logic (EWMA
// smoothing, open/close hysteresis with a close hold-off, optional rise-rate fast
// open for fast approaches) so it host-tests without a radio; the reader feeds it
// connection RSSI samples and defers Reader-Status-AP-Completed until it opens.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tuning knobs. Kconfig supplies the build defaults (ALIRO_RSSI_GATE_CFG_DEFAULT);
 * every threshold is a placeholder until the bench power/latency curve pins it. */
struct aliro_rssi_gate_cfg {
	int16_t open_dbm;         /* smoothed RSSI at/above this -> open */
	int16_t close_dbm;        /* smoothed RSSI at/below this (sustained) -> close */
	uint16_t close_hold_ms;   /* how long below close_dbm before closing */
	uint8_t ewma_shift;       /* smoothing alpha = 1/2^shift */
	uint8_t slope_open_db;    /* fast-open on this much rise per window; 0 = off */
	uint16_t slope_window_ms; /* rise-rate reference window */
};

/* Kconfig-tunable defaults, with host-build fallbacks (no sdkconfig there). */
#ifndef CONFIG_WOZ_RSSI_GATE_OPEN_DBM
#define CONFIG_WOZ_RSSI_GATE_OPEN_DBM -65
#endif
#ifndef CONFIG_WOZ_RSSI_GATE_CLOSE_DBM
#define CONFIG_WOZ_RSSI_GATE_CLOSE_DBM -75
#endif
#ifndef CONFIG_WOZ_RSSI_GATE_CLOSE_HOLD_MS
#define CONFIG_WOZ_RSSI_GATE_CLOSE_HOLD_MS 3000
#endif
#ifndef CONFIG_WOZ_RSSI_GATE_SLOPE_DB
#define CONFIG_WOZ_RSSI_GATE_SLOPE_DB 8
#endif

#define ALIRO_RSSI_GATE_CFG_DEFAULT                                                                \
	{                                                                                          \
		.open_dbm = CONFIG_WOZ_RSSI_GATE_OPEN_DBM,                                         \
		.close_dbm = CONFIG_WOZ_RSSI_GATE_CLOSE_DBM,                                       \
		.close_hold_ms = CONFIG_WOZ_RSSI_GATE_CLOSE_HOLD_MS,                               \
		.ewma_shift = 2u,                                                                  \
		.slope_open_db = CONFIG_WOZ_RSSI_GATE_SLOPE_DB,                                    \
		.slope_window_ms = 1500u,                                                          \
	}

/* HCI "RSSI not available" sentinel (Core spec Read RSSI); such samples are ignored. */
#define ALIRO_RSSI_UNAVAILABLE 127

/* Gate state. All-zeroes == reset/closed, so a zeroed session slot needs no init
 * call. Internal values are Q4 fixed point (dBm * 16). */
struct aliro_rssi_gate {
	bool primed; /* EWMA seeded by a first sample */
	bool open;
	int32_t ewma_q4;
	/* close hold-off tracking */
	bool below;
	uint32_t below_since_ms;
	/* rise-rate reference: `old` is between window/2 and ~window old */
	bool old_valid;
	bool mid_valid;
	int32_t old_q4;
	int32_t mid_q4;
	uint32_t old_ms;
	uint32_t mid_ms;
};

/* Return the gate to its zeroed (closed, unprimed) state. */
void aliro_rssi_gate_reset(struct aliro_rssi_gate *g);

/* Feed one connection RSSI sample (dBm, ALIRO_RSSI_UNAVAILABLE ignored) stamped
 * with a monotonic ms clock (wrap-safe). Returns the resulting open state. */
bool aliro_rssi_gate_feed(struct aliro_rssi_gate *g, const struct aliro_rssi_gate_cfg *cfg,
			  int8_t rssi_dbm, uint32_t now_ms);

/* Current open state without feeding a sample. */
bool aliro_rssi_gate_is_open(const struct aliro_rssi_gate *g);

/* Smoothed level in whole dBm (truncated), 0 before the first sample. Logging. */
int16_t aliro_rssi_gate_level_dbm(const struct aliro_rssi_gate *g);

#ifdef __cplusplus
}
#endif
