/**
 * @file test_woz_door.c — woz_door suite: the law-of-cosines angle mapping and
 * the closed/ajar/open state machine.
 *
 * Self-validating rather than golden-driven: the angle is checked by ROUND
 * TRIP, generating a distance from a known angle with the same law of cosines
 * in double precision and asserting the integer implementation recovers it. A
 * golden table would only prove the code still does what it did.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "woz_door.h"

/* An 850 mm door with both anchors the same distance from the hinge, which is
 * the mounting the module requires. theta0 is 0: with a == b the shut door puts
 * the two anchors together. */
#define DOOR_MM 850

static const struct woz_door_cfg k_cfg = {
	.hinge_to_frame_mm = DOOR_MM,
	.hinge_to_leaf_mm = DOOR_MM,
	.offset_mddeg = 0,
};

/** The distance a real door at @p deg would present, by the same law of cosines. */
static int32_t d_for_deg(const struct woz_door_cfg *c, double deg)
{
	double a = c->hinge_to_frame_mm;
	double b = c->hinge_to_leaf_mm;
	double phi = (deg * 1000.0 + c->offset_mddeg) / 1000.0 * M_PI / 180.0;

	return (int32_t)(sqrt(a * a + b * b - 2 * a * b * cos(phi)) + 0.5);
}

static void angle_roundtrip(void)
{
	t_group("angle: round trip against the double-precision geometry");

	/* Every 5 degrees across the usable span. The tolerance widens with the
	 * angle because the geometry itself goes blind as the door opens -- that
	 * is a property of a swinging door, not of this arithmetic. */
	for (int deg = 0; deg <= 120; deg += 5) {
		int32_t d = d_for_deg(&k_cfg, deg);
		int32_t got = woz_door_angle_mddeg(&k_cfg, d);
		int32_t want = deg * 1000;
		int32_t tol = (deg <= 90) ? 700 : 2000;
		char nm[48];

		snprintf(nm, sizeof(nm), "angle.%ddeg", deg);
		T_OK(nm, got != WOZ_DOOR_ANGLE_INVALID && labs(got - want) <= tol);
	}
}

static void angle_rejects_impossible(void)
{
	t_group("angle: a distance the hinge cannot explain is refused");

	/* The geometry can only produce d in [|a-b|, a+b] = [0, 1700] here. */
	T_EQ("angle.too_far", woz_door_angle_mddeg(&k_cfg, 2500), WOZ_DOOR_ANGLE_INVALID);
	T_EQ("angle.negative", woz_door_angle_mddeg(&k_cfg, -1), WOZ_DOOR_ANGLE_INVALID);
	T_EQ("angle.null_cfg", woz_door_angle_mddeg(NULL, 500), WOZ_DOOR_ANGLE_INVALID);

	/* Exactly at the limits is legal, and saturates rather than failing. */
	T_EQ("angle.shut", woz_door_angle_mddeg(&k_cfg, 0), 0);
	T_OK("angle.flat", woz_door_angle_mddeg(&k_cfg, 2 * DOOR_MM) >= 179000);
}

/*
 * THE MOUNTING REQUIREMENT, asserted rather than left in a comment.
 *
 * woz_door.h claims a and b must be roughly equal or the closed-versus-ajar
 * call degrades past usefulness, and quotes 2.0 deg against 35.7 deg for a
 * 200 mm mismatch. If that claim is wrong the header is lying to whoever mounts
 * the hardware, so it is checked here.
 */
static void resolution_needs_equal_arms(void)
{
	struct woz_door_cfg lop = {
		.hinge_to_frame_mm = 850, .hinge_to_leaf_mm = 650, .offset_mddeg = 0};
	int32_t good_shut, bad_shut, good_open;

	t_group("resolution: equal arms are a requirement, not a preference");

	good_shut = woz_door_resolution_mddeg(&k_cfg, 1000, 30);
	bad_shut = woz_door_resolution_mddeg(&lop, 1000, 30);
	good_open = woz_door_resolution_mddeg(&k_cfg, 35000, 30);

	/* Equal arms: about 2 degrees per 30 mm of jitter near shut. */
	T_OK("res.equal.shut_usable", good_shut > 1500 && good_shut < 2500);
	/* And it holds across the band the state machine actually uses. */
	T_OK("res.equal.open_usable", good_open > 1500 && good_open < 2600);
	/* Mismatched arms: an order of magnitude worse in exactly the place the
	 * closed/ajar decision lives. */
	T_OK("res.lopsided.shut_bad", bad_shut > 10 * good_shut);

	/* Fully open is blind, and the module says so instead of inventing a
	 * number. This is why nothing depends on telling 170 from 180 degrees. */
	T_EQ("res.flat_is_blind", woz_door_resolution_mddeg(&k_cfg, 180000, 30), INT32_MAX);
	T_EQ("res.null", woz_door_resolution_mddeg(NULL, 0, 30), INT32_MAX);
	T_EQ("res.zero_jitter", woz_door_resolution_mddeg(&k_cfg, 1000, 0), INT32_MAX);
}

/** Feed one angle n times; returns the state after. */
static enum woz_door_state feed_deg(struct woz_door *d, double deg, int n)
{
	enum woz_door_state s = WOZ_DOOR_UNKNOWN;

	for (int i = 0; i < n; i++) {
		s = woz_door_feed(d, d_for_deg(&d->cfg, deg));
	}
	return s;
}

static void state_machine(void)
{
	struct woz_door d;

	t_group("state machine: dwell and hysteresis");

	T_OK("sm.init", woz_door_init(&d, &k_cfg, NULL));
	T_EQ("sm.starts_unknown", d.state, WOZ_DOOR_UNKNOWN);

	/* Dwell is 3: two samples are not enough to commit, the third is. */
	T_EQ("sm.one_sample_holds", feed_deg(&d, 0.0, 1), WOZ_DOOR_UNKNOWN);
	T_EQ("sm.two_samples_hold", feed_deg(&d, 0.0, 1), WOZ_DOOR_UNKNOWN);
	T_EQ("sm.third_commits", feed_deg(&d, 0.0, 1), WOZ_DOOR_CLOSED);

	/* Hysteresis: 3 degrees is past closed_enter (2) but not past
	 * closed_leave (5), so a closed door stays closed. */
	T_EQ("sm.hysteresis_holds_closed", feed_deg(&d, 3.0, 5), WOZ_DOOR_CLOSED);

	/* Past closed_leave it becomes ajar, after the dwell. */
	T_EQ("sm.opens_to_ajar", feed_deg(&d, 15.0, 3), WOZ_DOOR_AJAR);

	/* Past open_enter (35) it becomes open. */
	T_EQ("sm.reaches_open", feed_deg(&d, 45.0, 3), WOZ_DOOR_OPEN);

	/* And 32 degrees is below open_enter but above open_leave (30), so an
	 * open door stays open -- the same hysteresis from the other side. */
	T_EQ("sm.hysteresis_holds_open", feed_deg(&d, 32.0, 5), WOZ_DOOR_OPEN);
	T_EQ("sm.closes_to_ajar", feed_deg(&d, 20.0, 3), WOZ_DOOR_AJAR);
	T_EQ("sm.back_to_closed", feed_deg(&d, 0.0, 3), WOZ_DOOR_CLOSED);
}

static void state_survives_a_bad_read(void)
{
	struct woz_door d;

	t_group("state machine: one impossible reading does not unset the door");

	T_OK("bad.init", woz_door_init(&d, &k_cfg, NULL));
	T_EQ("bad.closed", feed_deg(&d, 0.0, 3), WOZ_DOOR_CLOSED);

	/* A distance no hinge can produce. The committed state must survive: a
	 * single dropped frame announcing that a closed door is now unknown
	 * would be worse than saying nothing. */
	T_EQ("bad.holds_state", woz_door_feed(&d, 9999), WOZ_DOOR_CLOSED);
	T_EQ("bad.angle_invalid", d.last_mddeg, WOZ_DOOR_ANGLE_INVALID);

	/* And it breaks the run, so a half-finished transition has to start over. */
	T_EQ("bad.still_closed", feed_deg(&d, 45.0, 2), WOZ_DOOR_CLOSED);
	T_EQ("bad.then_commits", feed_deg(&d, 45.0, 1), WOZ_DOOR_OPEN);
}

static void rejects_bad_config(void)
{
	struct woz_door d;
	struct woz_door_cfg bad = {.hinge_to_frame_mm = 0, .hinge_to_leaf_mm = 850};

	t_group("config: refuses geometry it cannot use");
	T_OK("cfg.zero_arm", !woz_door_init(&d, &bad, NULL));
	T_OK("cfg.null_cfg", !woz_door_init(&d, NULL, NULL));
	T_OK("cfg.null_self", !woz_door_init(NULL, &k_cfg, NULL));
}

void test_woz_door(void)
{
	angle_roundtrip();
	angle_rejects_impossible();
	resolution_needs_equal_arms();
	state_machine();
	state_survives_a_bad_read();
	rejects_bad_config();
}
