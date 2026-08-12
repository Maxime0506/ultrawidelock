/**
 * @file test_rssi_gate.c — ultrawidelock_rssi_gate suite: synthetic walk-up RSSI traces
 * through the ranging power gate. Self-validating (no goldens): asserts the gate
 * opens at the door but not from the far field, the rise-rate fast open beats the
 * threshold open on a fast approach, fades don't flap the radio, and the ms clock
 * surviving a uint32 wrap.
 */
#include <string.h>

#include "test.h"

#include "ultrawidelock_rssi_gate.h"

/* One knob set for the whole suite: the build defaults with a 250 ms poll interval. */
#define STEP_MS 250u

static const struct ultrawidelock_rssi_gate_cfg k_cfg = ULTRAWIDELOCK_RSSI_GATE_CFG_DEFAULT;

/* Feed `n` samples of a fixed level, returning the final open state. */
static bool feed_flat(struct ultrawidelock_rssi_gate *g,
		      const struct ultrawidelock_rssi_gate_cfg *cfg, int8_t dbm, int n,
		      uint32_t *now_ms)
{
	bool open = false;

	for (int i = 0; i < n; i++) {
		open = ultrawidelock_rssi_gate_feed(g, cfg, dbm, *now_ms);
		*now_ms += STEP_MS;
	}
	return open;
}

/* Feed a linear ramp from `from` by `step_db` per sample until `to` is passed or
 * the gate opens; returns the sample index the gate opened at, or -1. */
static int feed_ramp_until_open(struct ultrawidelock_rssi_gate *g,
				const struct ultrawidelock_rssi_gate_cfg *cfg, int from,
				int step_db, int to, uint32_t *now_ms)
{
	int i = 0;

	for (int dbm = from; (step_db > 0) ? (dbm <= to) : (dbm >= to); dbm += step_db, i++) {
		if (ultrawidelock_rssi_gate_feed(g, cfg, (int8_t)dbm, *now_ms)) {
			return i;
		}
		*now_ms += STEP_MS;
	}
	return -1;
}

void test_rssi_gate(void)
{
	uint32_t now;
	struct ultrawidelock_rssi_gate g;

	t_group("rssi_gate: zeroed slot is closed + unprimed");
	memset(&g, 0, sizeof(g));
	T_OK("closed", !ultrawidelock_rssi_gate_is_open(&g));
	T_EQ("level 0", ultrawidelock_rssi_gate_level_dbm(&g), 0);

	t_group("rssi_gate: connect at the door opens on sample one");
	ultrawidelock_rssi_gate_reset(&g);
	now = 1000u;
	T_OK("open@first", ultrawidelock_rssi_gate_feed(&g, &k_cfg, -55, now));
	T_EQ("level seeded", ultrawidelock_rssi_gate_level_dbm(&g), -55);

	t_group("rssi_gate: far field never opens");
	ultrawidelock_rssi_gate_reset(&g);
	now = 0u;
	/* -85 with deterministic +/-4 dB noise, 200 samples (50 s parked far). */
	for (int i = 0; i < 200; i++) {
		int8_t dbm = (int8_t)(-85 + ((i * 7) % 9) - 4);

		T_OK("stays closed", !ultrawidelock_rssi_gate_feed(&g, &k_cfg, dbm, now));
		now += STEP_MS;
	}

	t_group("rssi_gate: slow approach opens at the smoothed threshold");
	struct ultrawidelock_rssi_gate_cfg cfg_noslope = k_cfg;

	cfg_noslope.slope_open_db = 0u; /* threshold-only */
	ultrawidelock_rssi_gate_reset(&g);
	now = 0u;
	int slow_open = feed_ramp_until_open(&g, &cfg_noslope, -90, 1, -40, &now);

	T_OK("opened", slow_open >= 0);
	T_OK("smoothed at/above open_dbm", ultrawidelock_rssi_gate_level_dbm(&g) >= k_cfg.open_dbm);
	/* The ramp starts at -90 and rises 1 dB per sample, so the raw level reaches
	 * open_dbm at sample (open_dbm + 90). Smoothing lags, so the gate can open at
	 * that sample but never before it. */
	T_OK("not before raw crossing", slow_open >= k_cfg.open_dbm + 90);

	t_group("rssi_gate: rise-rate fast open beats threshold on a fast approach");
	struct ultrawidelock_rssi_gate ga, gb;
	uint32_t now_a = 0u, now_b = 0u;

	ultrawidelock_rssi_gate_reset(&ga);
	ultrawidelock_rssi_gate_reset(&gb);
	/* 2 dB per 250 ms = 8 dB/s, a fast walk through the last ~15 m */
	int open_slope = feed_ramp_until_open(&ga, &k_cfg, -90, 2, -30, &now_a);
	int open_thresh = feed_ramp_until_open(&gb, &cfg_noslope, -90, 2, -30, &now_b);

	T_OK("slope opened", open_slope >= 0);
	T_OK("thresh opened", open_thresh >= 0);
	T_OK("slope earlier", open_slope < open_thresh);
	T_OK("slope floor held", ultrawidelock_rssi_gate_level_dbm(&ga) > k_cfg.close_dbm);

	t_group("rssi_gate: hysteresis band holds current state");
	/* Mid-band, derived from the configured thresholds rather than written in:
	 * a literal here silently stops testing hysteresis the moment the bench
	 * curve moves the band. */
	int8_t mid = (int8_t)((k_cfg.open_dbm + k_cfg.close_dbm) / 2);

	ultrawidelock_rssi_gate_reset(&g);
	now = 0u;
	T_OK("still closed mid-band", !feed_flat(&g, &k_cfg, mid, 40, &now));
	ultrawidelock_rssi_gate_reset(&g);
	(void)ultrawidelock_rssi_gate_feed(&g, &k_cfg, (int8_t)(k_cfg.open_dbm + 5), now); /* open */
	T_OK("still open mid-band", feed_flat(&g, &k_cfg, mid, 40, &now));

	t_group("rssi_gate: a short fade does not flap; a sustained drop closes");
	ultrawidelock_rssi_gate_reset(&g);
	now = 0u;
	T_OK("open near", ultrawidelock_rssi_gate_feed(&g, &k_cfg, -50, now));
	now += STEP_MS;
	/* 2.5 s at -85: below close_dbm but shorter than the 3 s hold */
	T_OK("fade survives", feed_flat(&g, &k_cfg, -85, 10, &now));
	T_OK("recovers", feed_flat(&g, &k_cfg, -55, 4, &now));
	/* sustained -85 well past the hold closes the gate */
	T_OK("departs closed", !feed_flat(&g, &k_cfg, -85, 40, &now));

	t_group("rssi_gate: RSSI-unavailable sentinel is ignored");
	ultrawidelock_rssi_gate_reset(&g);
	now = 0u;
	(void)ultrawidelock_rssi_gate_feed(&g, &k_cfg, -55, now);
	int16_t before = ultrawidelock_rssi_gate_level_dbm(&g);

	now += STEP_MS;
	T_OK("state kept", ultrawidelock_rssi_gate_feed(&g, &k_cfg, ULTRAWIDELOCK_RSSI_UNAVAILABLE, now));
	T_EQ("level kept", ultrawidelock_rssi_gate_level_dbm(&g), before);

	t_group("rssi_gate: ms clock wrap mid-walk-up");
	ultrawidelock_rssi_gate_reset(&g);
	now = 0xFFFFFF00u; /* wraps within the first samples */
	T_OK("open across wrap", ultrawidelock_rssi_gate_feed(&g, &k_cfg, -50, now));
	now += STEP_MS;
	/* sustained drop whose hold interval spans the wrap still closes */
	T_OK("close across wrap", !feed_flat(&g, &k_cfg, -85, 40, &now));

	t_group("rssi_gate: hold cap opens a gate the level never would");
	{
		/* Same tuning, but with the AP-Completed hold capped. */
		struct ultrawidelock_rssi_gate_cfg cap_cfg = k_cfg;

		cap_cfg.max_hold_ms = 1500u;
		cap_cfg.slope_open_db = 0u; /* isolate the cap from the fast-open path */

		ultrawidelock_rssi_gate_reset(&g);
		now = 0u;
		(void)ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -85, now);
		T_OK("closed while far", !ultrawidelock_rssi_gate_is_open(&g));

		/* No hold started yet: far samples alone must never open it, however
		 * many arrive. This is the pre-AUTH stretch of a walk-up. */
		T_OK("no cap without a hold", !feed_flat(&g, &cap_cfg, -85, 20, &now));

		ultrawidelock_rssi_gate_hold_begin(&g, now);
		T_OK("hold does not open on its own", !ultrawidelock_rssi_gate_is_open(&g));

		now += 1000u; /* inside the cap */
		T_OK("still held before the cap", !ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -85, now));
		T_OK("not flagged capped yet", !ultrawidelock_rssi_gate_was_capped(&g));

		now += 600u; /* 1600 ms of hold: past it */
		T_OK("opens at the cap", ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -85, now));
		T_OK("flagged as capped", ultrawidelock_rssi_gate_was_capped(&g));

		/* The point of opening rather than bypassing the gate: the ordinary
		 * close path must still reclaim the radio, or a capped hold would leave
		 * the DW3000 up until the peer disconnects. */
		now += STEP_MS;
		T_OK("capped open still closes on a fade", !feed_flat(&g, &cap_cfg, -85, 40, &now));

		/* A level that qualifies on its own is not a capped open. */
		ultrawidelock_rssi_gate_reset(&g);
		now = 0u;
		ultrawidelock_rssi_gate_hold_begin(&g, now);
		T_OK("level opens before the cap", ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -50, now));
		T_OK("level open is not capped", !ultrawidelock_rssi_gate_was_capped(&g));

		/* max_hold_ms = 0 keeps the old unbounded hold. */
		cap_cfg.max_hold_ms = 0u;
		ultrawidelock_rssi_gate_reset(&g);
		now = 0u;
		(void)ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -85, now);
		ultrawidelock_rssi_gate_hold_begin(&g, now);
		now += 60000u;
		T_OK("cap disabled holds indefinitely",
		     !ultrawidelock_rssi_gate_feed(&g, &cap_cfg, -85, now));
	}

	t_group("rssi_gate: reset returns to zeroed");
	ultrawidelock_rssi_gate_reset(&g);
	T_OK("closed after reset", !ultrawidelock_rssi_gate_is_open(&g));
	T_EQ("level cleared", ultrawidelock_rssi_gate_level_dbm(&g), 0);
}
