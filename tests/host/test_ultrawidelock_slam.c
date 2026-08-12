/**
 * @file test_ultrawidelock_slam.c — ultrawidelock_slam suite: debounce, burst windows and the
 * tamper latch, driven entirely by a fake clock.
 *
 * The classifier's whole job is to turn "a pin went high" into "someone is
 * attacking this door" without crying wolf, and every way it can get that wrong
 * is a timing case. So the interesting assertions here are the ones that hold
 * the clock still, run it backwards, or start it a month after boot.
 */
#include "test.h"

#include "ultrawidelock_slam.h"

static const struct ultrawidelock_slam_cfg k_cfg = {
	.debounce_ms = ULTRAWIDELOCK_SLAM_DEBOUNCE_MS_DEFAULT,      /* 400 */
	.tamper_window_ms = ULTRAWIDELOCK_SLAM_TAMPER_WINDOW_MS_DEFAULT, /* 4000 */
	.tamper_count = ULTRAWIDELOCK_SLAM_TAMPER_COUNT_DEFAULT,    /* 3 */
};

static void quiet_door_stays_quiet(void)
{
	struct ultrawidelock_slam_state s;
	int i;

	t_group("slam: no strike, no event, ever");

	ultrawidelock_slam_init(&s);
	for (i = 0; i < 100; i++) {
		T_EQ("quiet.none", ultrawidelock_slam_poll(&k_cfg, &s, false, i * 250), ULTRAWIDELOCK_SLAM_NONE);
	}
	T_OK("quiet.not_tampered", !ultrawidelock_slam_tampered(&s));
}

static void one_strike_is_an_impact(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: a single strike is an impact and nothing more");

	ultrawidelock_slam_init(&s);
	T_EQ("one.impact", ultrawidelock_slam_poll(&k_cfg, &s, true, 10000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_OK("one.not_tampered", !ultrawidelock_slam_tampered(&s));
	/* Silence afterwards must not decay into anything. */
	T_EQ("one.quiet_after", ultrawidelock_slam_poll(&k_cfg, &s, false, 20000),
	     ULTRAWIDELOCK_SLAM_NONE);
}

/*
 * THE BUG THIS EXISTS TO CATCH. A zeroed last_accept_ms happens to behave at
 * boot, because uptime is smaller than the debounce for the first 400 ms, and
 * then quietly stops behaving for every board that has been up longer. Without
 * the `seeded` flag the first strike on a board with three weeks of uptime is
 * silently swallowed -- and it would pass any test that starts its clock at 0.
 */
static void first_strike_is_accepted_at_any_uptime(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: the first strike counts even on a board up for weeks");

	ultrawidelock_slam_init(&s);
	/* Three weeks of uptime in milliseconds. */
	T_EQ("seed.late_boot", ultrawidelock_slam_poll(&k_cfg, &s, true, 1814400000LL),
	     ULTRAWIDELOCK_SLAM_IMPACT);
}

static void debounce_absorbs_the_ringing(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: one impact rings, and the ringing is not more impacts");

	ultrawidelock_slam_init(&s);
	T_EQ("deb.first", ultrawidelock_slam_poll(&k_cfg, &s, true, 10000), ULTRAWIDELOCK_SLAM_IMPACT);
	/* Rebound, latch chatter, the leaf settling: all inside 400 ms. */
	T_EQ("deb.plus_100", ultrawidelock_slam_poll(&k_cfg, &s, true, 10100), ULTRAWIDELOCK_SLAM_NONE);
	T_EQ("deb.plus_250", ultrawidelock_slam_poll(&k_cfg, &s, true, 10250), ULTRAWIDELOCK_SLAM_NONE);
	T_EQ("deb.plus_399", ultrawidelock_slam_poll(&k_cfg, &s, true, 10399), ULTRAWIDELOCK_SLAM_NONE);
	/* Exactly at the boundary is accepted: the comparison is strict. */
	T_EQ("deb.plus_400", ultrawidelock_slam_poll(&k_cfg, &s, true, 10400), ULTRAWIDELOCK_SLAM_IMPACT);

	/* And the debounce restarts from the ACCEPTED strike, not the poll. */
	T_EQ("deb.restart", ultrawidelock_slam_poll(&k_cfg, &s, true, 10500), ULTRAWIDELOCK_SLAM_NONE);
}

static void repeated_strikes_latch_tamper(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: three strikes inside the window is intent");

	ultrawidelock_slam_init(&s);
	T_EQ("tam.1", ultrawidelock_slam_poll(&k_cfg, &s, true, 1000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("tam.2", ultrawidelock_slam_poll(&k_cfg, &s, true, 1500), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("tam.3", ultrawidelock_slam_poll(&k_cfg, &s, true, 2000), ULTRAWIDELOCK_SLAM_TAMPER);
	T_OK("tam.latched", ultrawidelock_slam_tampered(&s));

	/*
	 * Reported once, not once per strike. Otherwise how many times the door
	 * node logs is the attacker's choice, on a board whose console is a 4 KB
	 * ring.
	 */
	T_EQ("tam.4_silent", ultrawidelock_slam_poll(&k_cfg, &s, true, 2500), ULTRAWIDELOCK_SLAM_NONE);
	T_EQ("tam.5_silent", ultrawidelock_slam_poll(&k_cfg, &s, true, 3000), ULTRAWIDELOCK_SLAM_NONE);
	T_OK("tam.still_latched", ultrawidelock_slam_tampered(&s));

	/* And it does not clear itself with time: that decision is the caller's. */
	T_EQ("tam.much_later", ultrawidelock_slam_poll(&k_cfg, &s, false, 900000),
	     ULTRAWIDELOCK_SLAM_NONE);
	T_OK("tam.survives_time", ultrawidelock_slam_tampered(&s));

	ultrawidelock_slam_clear_tamper(&s);
	T_OK("tam.cleared", !ultrawidelock_slam_tampered(&s));
}

static void spaced_strikes_are_not_tamper(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: strikes spread past the window never accumulate");

	ultrawidelock_slam_init(&s);
	/* Every strike is 5 s apart: outside the 4 s window, so the count keeps
	 * resetting. This is a door being used, not attacked. */
	T_EQ("sp.1", ultrawidelock_slam_poll(&k_cfg, &s, true, 0), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("sp.2", ultrawidelock_slam_poll(&k_cfg, &s, true, 5000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("sp.3", ultrawidelock_slam_poll(&k_cfg, &s, true, 10000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("sp.4", ultrawidelock_slam_poll(&k_cfg, &s, true, 15000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_OK("sp.never_tampered", !ultrawidelock_slam_tampered(&s));
}

static void window_boundary(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: the burst window boundary is where it says it is");

	/* Two inside, third just inside the window: tamper. */
	ultrawidelock_slam_init(&s);
	T_EQ("wb.in.1", ultrawidelock_slam_poll(&k_cfg, &s, true, 0), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("wb.in.2", ultrawidelock_slam_poll(&k_cfg, &s, true, 1000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("wb.in.3", ultrawidelock_slam_poll(&k_cfg, &s, true, 4000), ULTRAWIDELOCK_SLAM_TAMPER);

	/* Same shape, third just outside: the window restarts and it is only an
	 * impact. One millisecond decides it, and that is the intended behaviour
	 * rather than a rounding accident. */
	ultrawidelock_slam_init(&s);
	T_EQ("wb.out.1", ultrawidelock_slam_poll(&k_cfg, &s, true, 0), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("wb.out.2", ultrawidelock_slam_poll(&k_cfg, &s, true, 1000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("wb.out.3", ultrawidelock_slam_poll(&k_cfg, &s, true, 4001), ULTRAWIDELOCK_SLAM_IMPACT);
	T_OK("wb.out.not_tampered", !ultrawidelock_slam_tampered(&s));
}

/*
 * A clock that jumps backwards is not a door being hit. It is the caller's
 * timebase moving -- a settings restore, a resync, a test harness. The
 * classifier must not swallow a real strike because of it, so it accepts.
 */
static void backwards_clock_accepts_rather_than_drops(void)
{
	struct ultrawidelock_slam_state s;

	t_group("slam: a clock that moves backwards does not eat a strike");

	ultrawidelock_slam_init(&s);
	T_EQ("back.first", ultrawidelock_slam_poll(&k_cfg, &s, true, 100000), ULTRAWIDELOCK_SLAM_IMPACT);
	T_EQ("back.jumped", ultrawidelock_slam_poll(&k_cfg, &s, true, 500), ULTRAWIDELOCK_SLAM_IMPACT);
	/* And the state is coherent afterwards: the debounce now runs from the
	 * new timebase, not the abandoned one. */
	T_EQ("back.debounced", ultrawidelock_slam_poll(&k_cfg, &s, true, 600), ULTRAWIDELOCK_SLAM_NONE);
	T_EQ("back.resumes", ultrawidelock_slam_poll(&k_cfg, &s, true, 900), ULTRAWIDELOCK_SLAM_IMPACT);
}

static void rejects_bad_input(void)
{
	struct ultrawidelock_slam_state s;
	struct ultrawidelock_slam_cfg zero = {0, 0, 0};

	t_group("slam: nonsense in, NONE out");

	ultrawidelock_slam_init(&s);
	T_EQ("in.null_cfg", ultrawidelock_slam_poll(NULL, &s, true, 1000), ULTRAWIDELOCK_SLAM_NONE);
	T_EQ("in.null_state", ultrawidelock_slam_poll(&k_cfg, NULL, true, 1000), ULTRAWIDELOCK_SLAM_NONE);
	/* A zero tamper_count would otherwise make every strike instant tamper. */
	T_EQ("in.zero_count", ultrawidelock_slam_poll(&zero, &s, true, 1000), ULTRAWIDELOCK_SLAM_NONE);

	T_OK("in.null_tampered", !ultrawidelock_slam_tampered(NULL));
	ultrawidelock_slam_clear_tamper(NULL); /* must not fault */
	ultrawidelock_slam_init(NULL);         /* must not fault */

	/* A zero debounce is legal and means "every poll counts", which is what
	 * a caller polling at 250 ms already gets. */
	{
		struct ultrawidelock_slam_cfg hot = {0u, 4000u, 3u};
		struct ultrawidelock_slam_state h;

		ultrawidelock_slam_init(&h);
		T_EQ("in.hot.1", ultrawidelock_slam_poll(&hot, &h, true, 1000), ULTRAWIDELOCK_SLAM_IMPACT);
		T_EQ("in.hot.2", ultrawidelock_slam_poll(&hot, &h, true, 1000), ULTRAWIDELOCK_SLAM_IMPACT);
		T_EQ("in.hot.3", ultrawidelock_slam_poll(&hot, &h, true, 1000), ULTRAWIDELOCK_SLAM_TAMPER);
	}
}

void test_ultrawidelock_slam(void)
{
	quiet_door_stays_quiet();
	one_strike_is_an_impact();
	first_strike_is_accepted_at_any_uptime();
	debounce_absorbs_the_ringing();
	repeated_strikes_latch_tamper();
	spaced_strikes_are_not_tamper();
	window_boundary();
	backwards_clock_accepts_rather_than_drops();
	rejects_bad_input();
}
