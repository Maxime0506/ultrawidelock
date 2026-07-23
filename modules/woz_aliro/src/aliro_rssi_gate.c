// BLE-RSSI ranging power gate implementation: EWMA smoothing in Q4 fixed point,
// open/close hysteresis with a sustained-below close hold, and an optional
// rise-rate fast open so a fast approach is not penalized by the smoothing lag.
// Pure logic — no radio, clock, or logging dependencies — so the host suite can
// drive it with synthetic approach traces.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>

#include "aliro_rssi_gate.h"

#define Q4 16 /* Q4 scale; multiply (never shift) — negatives are involved */

void aliro_rssi_gate_reset(struct aliro_rssi_gate *g)
{
	memset(g, 0, sizeof(*g));
}

bool aliro_rssi_gate_is_open(const struct aliro_rssi_gate *g)
{
	return g->open;
}

int16_t aliro_rssi_gate_level_dbm(const struct aliro_rssi_gate *g)
{
	return (int16_t)(g->ewma_q4 / Q4);
}

/* Advance the rise-rate reference: `mid` follows the newest smoothed value, and
 * once it is window/2 old it becomes `old`, so `old` always lags the present by
 * between window/2 and ~window (given a steady sample cadence). */
static void slope_track(struct aliro_rssi_gate *g, const struct aliro_rssi_gate_cfg *cfg,
			uint32_t now_ms)
{
	uint32_t half = (uint32_t)cfg->slope_window_ms / 2u;

	if (!g->mid_valid) {
		g->mid_q4 = g->ewma_q4;
		g->mid_ms = now_ms;
		g->mid_valid = true;
		return;
	}
	if (now_ms - g->mid_ms >= half) {
		g->old_q4 = g->mid_q4;
		g->old_ms = g->mid_ms;
		g->old_valid = true;
		g->mid_q4 = g->ewma_q4;
		g->mid_ms = now_ms;
	}
}

bool aliro_rssi_gate_feed(struct aliro_rssi_gate *g, const struct aliro_rssi_gate_cfg *cfg,
			  int8_t rssi_dbm, uint32_t now_ms)
{
	if (rssi_dbm == ALIRO_RSSI_UNAVAILABLE) {
		return g->open;
	}

	int32_t sample_q4 = (int32_t)rssi_dbm * Q4;

	if (!g->primed) {
		/* First sample seeds the average outright: a walk-up that connects
		 * already at the door must open on sample one, not after the EWMA
		 * crawls up from zero. */
		g->ewma_q4 = sample_q4;
		g->primed = true;
	} else {
		/* Division, not shift: arithmetic right shift of negatives is
		 * implementation-defined and Q4 values are negative dBm. */
		g->ewma_q4 += (sample_q4 - g->ewma_q4) / (1 << cfg->ewma_shift);
	}
	slope_track(g, cfg, now_ms);

	if (!g->open) {
		bool open = g->ewma_q4 >= (int32_t)cfg->open_dbm * Q4;

		/* Rise-rate fast open: a fast approach climbs slope_open_db within
		 * the window while still shy of open_dbm; open early rather than
		 * spend that walking time smoothing. Floor at close_dbm so noise
		 * at the fringe cannot fast-open. */
		if (!open && cfg->slope_open_db > 0u && g->old_valid &&
		    now_ms - g->old_ms <= 2u * (uint32_t)cfg->slope_window_ms &&
		    g->ewma_q4 - g->old_q4 >= (int32_t)cfg->slope_open_db * Q4 &&
		    g->ewma_q4 > (int32_t)cfg->close_dbm * Q4) {
			open = true;
		}
		if (open) {
			g->open = true;
			g->below = false;
		}
		return g->open;
	}

	/* Open: close only on a sustained drop below close_dbm, so a single deep
	 * fade (body blocking, pocket) cannot flap the radio off mid-approach. */
	if (g->ewma_q4 <= (int32_t)cfg->close_dbm * Q4) {
		if (!g->below) {
			g->below = true;
			g->below_since_ms = now_ms;
		} else if (now_ms - g->below_since_ms >= cfg->close_hold_ms) {
			g->open = false;
			g->below = false;
		}
	} else {
		g->below = false;
	}
	return g->open;
}
