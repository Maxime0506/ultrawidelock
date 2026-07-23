/**
 * @file test_rssi_gate.c — aliro_rssi_gate suite: synthetic walk-up RSSI traces
 * through the ranging power gate. Self-validating (no goldens): asserts the gate
 * opens at the door but not from the far field, the rise-rate fast open beats the
 * threshold open on a fast approach, fades don't flap the radio, and the ms clock
 * surviving a uint32 wrap.
 */
#include <string.h>

#include "test.h"

#include "aliro_rssi_gate.h"

/* One knob set for the whole suite: the build defaults with a 250 ms cadence. */
#define STEP_MS 250u

static const struct aliro_rssi_gate_cfg k_cfg = ALIRO_RSSI_GATE_CFG_DEFAULT;

/* Feed `n` samples of a fixed level, returning the final open state. */
static bool feed_flat(struct aliro_rssi_gate *g, const struct aliro_rssi_gate_cfg *cfg,
		      int8_t dbm, int n, uint32_t *now_ms)
{
	bool open = false;

	for (int i = 0; i < n; i++) {
		open = aliro_rssi_gate_feed(g, cfg, dbm, *now_ms);
		*now_ms += STEP_MS;
	}
	return open;
}

/* Feed a linear ramp from `from` by `step_db` per sample until `to` is passed or
 * the gate opens; returns the sample index the gate opened at, or -1. */
static int feed_ramp_until_open(struct aliro_rssi_gate *g, const struct aliro_rssi_gate_cfg *cfg,
				int from, int step_db, int to, uint32_t *now_ms)
{
	int i = 0;

	for (int dbm = from; (step_db > 0) ? (dbm <= to) : (dbm >= to); dbm += step_db, i++) {
		if (aliro_rssi_gate_feed(g, cfg, (int8_t)dbm, *now_ms)) {
			return i;
		}
		*now_ms += STEP_MS;
	}
	return -1;
}

void test_rssi_gate(void)
{
	uint32_t now;
	struct aliro_rssi_gate g;

	t_group("rssi_gate: zeroed slot is closed + unprimed");
	memset(&g, 0, sizeof(g));
	T_OK("closed", !aliro_rssi_gate_is_open(&g));
	T_EQ("level 0", aliro_rssi_gate_level_dbm(&g), 0);

	t_group("rssi_gate: connect at the door opens on sample one");
	aliro_rssi_gate_reset(&g);
	now = 1000u;
	T_OK("open@first", aliro_rssi_gate_feed(&g, &k_cfg, -55, now));
	T_EQ("level seeded", aliro_rssi_gate_level_dbm(&g), -55);

	t_group("rssi_gate: far field never opens");
	aliro_rssi_gate_reset(&g);
	now = 0u;
	/* -85 with deterministic +/-4 dB noise, 200 samples (50 s parked far). */
	for (int i = 0; i < 200; i++) {
		int8_t dbm = (int8_t)(-85 + ((i * 7) % 9) - 4);

		T_OK("stays closed", !aliro_rssi_gate_feed(&g, &k_cfg, dbm, now));
		now += STEP_MS;
	}

	t_group("rssi_gate: slow approach opens at the smoothed threshold");
	struct aliro_rssi_gate_cfg cfg_noslope = k_cfg;

	cfg_noslope.slope_open_db = 0u; /* threshold-only */
	aliro_rssi_gate_reset(&g);
	now = 0u;
	int slow_open = feed_ramp_until_open(&g, &cfg_noslope, -90, 1, -40, &now);

	T_OK("opened", slow_open >= 0);
	T_OK("smoothed at/above open_dbm", aliro_rssi_gate_level_dbm(&g) >= k_cfg.open_dbm);
	/* raw crosses -65 at sample 25; EWMA lag means never before that */
	T_OK("not before raw crossing", slow_open >= 25);

	t_group("rssi_gate: rise-rate fast open beats threshold on a fast approach");
	struct aliro_rssi_gate ga, gb;
	uint32_t now_a = 0u, now_b = 0u;

	aliro_rssi_gate_reset(&ga);
	aliro_rssi_gate_reset(&gb);
	/* 2 dB per 250 ms = 8 dB/s, a fast walk through the last ~15 m */
	int open_slope = feed_ramp_until_open(&ga, &k_cfg, -90, 2, -30, &now_a);
	int open_thresh = feed_ramp_until_open(&gb, &cfg_noslope, -90, 2, -30, &now_b);

	T_OK("slope opened", open_slope >= 0);
	T_OK("thresh opened", open_thresh >= 0);
	T_OK("slope earlier", open_slope < open_thresh);
	T_OK("slope floor held", aliro_rssi_gate_level_dbm(&ga) > k_cfg.close_dbm);

	t_group("rssi_gate: hysteresis band holds current state");
	aliro_rssi_gate_reset(&g);
	now = 0u;
	T_OK("still closed at -70", !feed_flat(&g, &k_cfg, -70, 40, &now));
	aliro_rssi_gate_reset(&g);
	(void)aliro_rssi_gate_feed(&g, &k_cfg, -55, now); /* open */
	T_OK("still open at -70", feed_flat(&g, &k_cfg, -70, 40, &now));

	t_group("rssi_gate: a short fade does not flap; a sustained drop closes");
	aliro_rssi_gate_reset(&g);
	now = 0u;
	T_OK("open near", aliro_rssi_gate_feed(&g, &k_cfg, -50, now));
	now += STEP_MS;
	/* 2.5 s at -85: below close_dbm but shorter than the 3 s hold */
	T_OK("fade survives", feed_flat(&g, &k_cfg, -85, 10, &now));
	T_OK("recovers", feed_flat(&g, &k_cfg, -55, 4, &now));
	/* sustained -85 well past the hold closes the gate */
	T_OK("departs closed", !feed_flat(&g, &k_cfg, -85, 40, &now));

	t_group("rssi_gate: RSSI-unavailable sentinel is ignored");
	aliro_rssi_gate_reset(&g);
	now = 0u;
	(void)aliro_rssi_gate_feed(&g, &k_cfg, -55, now);
	int16_t before = aliro_rssi_gate_level_dbm(&g);

	now += STEP_MS;
	T_OK("state kept", aliro_rssi_gate_feed(&g, &k_cfg, ALIRO_RSSI_UNAVAILABLE, now));
	T_EQ("level kept", aliro_rssi_gate_level_dbm(&g), before);

	t_group("rssi_gate: ms clock wrap mid-walk-up");
	aliro_rssi_gate_reset(&g);
	now = 0xFFFFFF00u; /* wraps within the first samples */
	T_OK("open across wrap", aliro_rssi_gate_feed(&g, &k_cfg, -50, now));
	now += STEP_MS;
	/* sustained drop whose hold interval spans the wrap still closes */
	T_OK("close across wrap", !feed_flat(&g, &k_cfg, -85, 40, &now));

	t_group("rssi_gate: reset returns to zeroed");
	aliro_rssi_gate_reset(&g);
	T_OK("closed after reset", !aliro_rssi_gate_is_open(&g));
	T_EQ("level cleared", aliro_rssi_gate_level_dbm(&g), 0);
}
