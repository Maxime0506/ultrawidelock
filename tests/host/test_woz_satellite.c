/**
 * @file test_woz_satellite.c — woz_satellite suite: the freshness gate, and the
 * mounting flag that decides which anchor is which.
 *
 * woz_fusion is already covered. What this suite is for is the two ways the
 * gate around it can be wrong in a way no geometry test would catch: deciding
 * with a stale distance, and deciding with the two anchors swapped.
 */
#include "test.h"

#include "woz_satellite.h"

/* Anchors 1.2 m apart, matching test_woz_fusion.c so the two suites agree on
 * what the geometry means. */
static const struct woz_fusion_cfg k_cfg = {
	.baseline_mm = 1200,
	.tol_mm = 90,
	.deadband_mm = 60,
};

static void absent_satellite_changes_nothing(void)
{
	struct woz_satellite s;

	t_group("sat: no satellite means today's behaviour, not a locked door");

	woz_satellite_init(&s, &k_cfg, 1500u, true);

	/* Never reported. This is the state a board is in for its whole life
	 * until Stage C exists, so it had better be the permissive one. */
	T_OK("absent.predicts", woz_satellite_may_predict(&s, 800, 10000));
	T_OK("absent.unknown", woz_satellite_verdict(&s, 800, 10000).side == WOZ_SIDE_UNKNOWN);
	T_OK("absent.not_ok", !woz_satellite_verdict(&s, 800, 10000).geometry_ok);

	/* And a NULL satellite, which is how a build without one behaves. */
	T_OK("absent.null_predicts", woz_satellite_may_predict(NULL, 800, 10000));
}

static void fresh_report_decides(void)
{
	struct woz_satellite s;
	struct woz_fusion_verdict v;

	t_group("sat: a fresh report is used");

	woz_satellite_init(&s, &k_cfg, 1500u, true);
	/* Phone nearer the outside anchor: self (inside) 1400, peer 800. */
	woz_satellite_report(&s, 800, 10000);

	v = woz_satellite_verdict(&s, 1400, 10000);
	T_OK("fresh.ok", v.geometry_ok);
	T_EQ("fresh.outside", v.side, WOZ_SIDE_OUTSIDE);
	T_OK("fresh.withholds", !woz_satellite_may_predict(&s, 1400, 10000));

	/* And the other way round. */
	woz_satellite_report(&s, 1400, 10000);
	T_EQ("fresh.inside", woz_satellite_verdict(&s, 800, 10000).side, WOZ_SIDE_INSIDE);
	T_OK("fresh.inside_predicts", woz_satellite_may_predict(&s, 800, 10000));
}

/*
 * THE FAILURE THIS SUITE EXISTS FOR. Identical numbers, opposite mounting.
 * Getting self_is_inside backwards does not crash, does not log, and does not
 * fail any geometry test -- it silently withholds from the householder and
 * grants to the person on the doorstep.
 */
static void mounting_flag_inverts_the_verdict(void)
{
	struct woz_satellite in, out;

	t_group("sat: self_is_inside is the difference between right and backwards");

	woz_satellite_init(&in, &k_cfg, 1500u, true);
	woz_satellite_init(&out, &k_cfg, 1500u, false);
	woz_satellite_report(&in, 800, 10000);
	woz_satellite_report(&out, 800, 10000);

	/* Same self_mm, same peer_mm, same instant. Only the mounting differs. */
	T_EQ("mount.inside_says", woz_satellite_verdict(&in, 1400, 10000).side, WOZ_SIDE_OUTSIDE);
	T_EQ("mount.outside_says", woz_satellite_verdict(&out, 1400, 10000).side, WOZ_SIDE_INSIDE);
	T_OK("mount.opposite_predict", woz_satellite_may_predict(&in, 1400, 10000) !=
					      woz_satellite_may_predict(&out, 1400, 10000));
}

static void stale_report_is_not_a_report(void)
{
	struct woz_satellite s;

	t_group("sat: an old distance is discarded, not trusted");

	woz_satellite_init(&s, &k_cfg, 1500u, true);
	woz_satellite_report(&s, 800, 10000);

	/* Inside the window: still decides, still withholds. */
	T_OK("stale.at_1499", !woz_satellite_may_predict(&s, 1400, 11499));
	T_OK("stale.at_1500", !woz_satellite_may_predict(&s, 1400, 11500));

	/* Past it: the verdict evaporates and prediction resumes. The phone has
	 * not moved; only the evidence has expired. */
	T_OK("stale.at_1501", woz_satellite_may_predict(&s, 1400, 11501));
	T_EQ("stale.side_gone", woz_satellite_verdict(&s, 1400, 11501).side, WOZ_SIDE_UNKNOWN);
	T_OK("stale.not_ok", !woz_satellite_verdict(&s, 1400, 11501).geometry_ok);

	/* A fresh report revives it. */
	woz_satellite_report(&s, 800, 11501);
	T_OK("stale.revived", !woz_satellite_may_predict(&s, 1400, 11501));
}

/*
 * A report timestamped in the future is a clock that moved, not a fast link.
 * Treating it as fresh would pin one stale distance as valid for as long as the
 * offset lasted, which is the one way a staleness window can fail open.
 */
static void future_report_is_refused(void)
{
	struct woz_satellite s;

	t_group("sat: a report from the future is unusable, not eternal");

	woz_satellite_init(&s, &k_cfg, 1500u, true);
	woz_satellite_report(&s, 800, 50000);

	T_OK("future.predicts", woz_satellite_may_predict(&s, 1400, 10000));
	T_EQ("future.unknown", woz_satellite_verdict(&s, 1400, 10000).side, WOZ_SIDE_UNKNOWN);
	/* Once the clock catches up it is usable again. */
	T_OK("future.recovers", !woz_satellite_may_predict(&s, 1400, 50000));
}

static void impossible_geometry_withholds(void)
{
	struct woz_satellite s;

	t_group("sat: a pair no phone position can produce is evidence, and withholds");

	woz_satellite_init(&s, &k_cfg, 1500u, true);
	/* 200 and 300 cannot span a 1200 baseline. */
	woz_satellite_report(&s, 300, 10000);
	T_OK("bad.not_ok", !woz_satellite_verdict(&s, 200, 10000).geometry_ok);
	T_OK("bad.withholds", !woz_satellite_may_predict(&s, 200, 10000));
}

static void rejects_bad_input(void)
{
	struct woz_satellite s;

	t_group("sat: nonsense in, permissive out");

	woz_satellite_init(&s, &k_cfg, 1500u, true);

	/* A negative peer distance is a decode bug. Refuse to store it, so it
	 * cannot masquerade as a stale link later. */
	woz_satellite_report(&s, -1, 10000);
	T_OK("in.negative_peer_not_stored", woz_satellite_may_predict(&s, 1400, 10000));

	woz_satellite_report(&s, 800, 10000);
	/* A negative self distance is the caller's bug; do not decide on it. */
	T_OK("in.negative_self", woz_satellite_may_predict(&s, -1, 10000));

	/* NULLs must not fault. */
	woz_satellite_init(NULL, &k_cfg, 1500u, true);
	woz_satellite_report(NULL, 800, 10000);
	T_OK("in.null_predicts", woz_satellite_may_predict(NULL, 800, 10000));

	/* Zero stale_ms selects the default rather than expiring instantly. */
	{
		struct woz_satellite d;

		woz_satellite_init(&d, &k_cfg, 0u, true);
		woz_satellite_report(&d, 800, 10000);
		T_OK("in.zero_stale_is_default",
		     !woz_satellite_may_predict(&d, 1400,
						10000 + WOZ_SATELLITE_STALE_MS_DEFAULT));
	}

	/* A NULL cfg zeroes the baseline, which woz_fusion rejects -- so the
	 * verdict is never OK and prediction is never withheld on garbage. */
	{
		struct woz_satellite n;

		woz_satellite_init(&n, NULL, 1500u, true);
		woz_satellite_report(&n, 800, 10000);
		T_OK("in.null_cfg_permits", woz_satellite_may_predict(&n, 1400, 10000));
	}
}

void test_woz_satellite(void)
{
	absent_satellite_changes_nothing();
	fresh_report_decides();
	mounting_flag_inverts_the_verdict();
	stale_report_is_not_a_report();
	future_report_is_refused();
	impossible_geometry_withholds();
	rejects_bad_input();
}
