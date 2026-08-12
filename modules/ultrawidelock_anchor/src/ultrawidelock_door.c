/**
 * @file ultrawidelock_door.c — door swing angle and state machine (implementation).
 */

#include "ultrawidelock_door.h"

/**
 * cos(theta) in Q15, one entry per degree, 0..180.
 *
 * One table serves both directions: cos by lookup with linear interpolation,
 * arccos by binary search over the same entries. 362 bytes, against a software
 * float acos plus libm on a part with no FPU.
 *
 * Generated, and reproducible: round(cos(radians(d)) * 32767) for d in 0..180.
 */
static const int16_t k_cos_q15[181] = {
	32767,  32762,  32747,  32722,  32687,  32642,  32587,  32523,  32448,  32364,  32269,
	32165,  32051,  31927,  31794,  31650,  31498,  31335,  31163,  30982,  30791,  30591,
	30381,  30162,  29934,  29697,  29451,  29196,  28932,  28659,  28377,  28087,  27788,
	27481,  27165,  26841,  26509,  26169,  25821,  25465,  25101,  24730,  24351,  23964,
	23571,  23170,  22762,  22347,  21925,  21497,  21062,  20621,  20173,  19720,  19260,
	18794,  18323,  17846,  17364,  16876,  16384,  15886,  15383,  14876,  14364,  13848,
	13328,  12803,  12275,  11743,  11207,  10668,  10126,  9580,   9032,   8481,   7927,
	7371,   6813,   6252,   5690,   5126,   4560,   3993,   3425,   2856,   2286,   1715,
	1144,   572,    0,      -572,   -1144,  -1715,  -2286,  -2856,  -3425,  -3993,  -4560,
	-5126,  -5690,  -6252,  -6813,  -7371,  -7927,  -8481,  -9032,  -9580,  -10126, -10668,
	-11207, -11743, -12275, -12803, -13328, -13848, -14364, -14876, -15383, -15886, -16383,
	-16876, -17364, -17846, -18323, -18794, -19260, -19720, -20173, -20621, -21062, -21497,
	-21925, -22347, -22762, -23170, -23571, -23964, -24351, -24730, -25101, -25465, -25821,
	-26169, -26509, -26841, -27165, -27481, -27788, -28087, -28377, -28659, -28932, -29196,
	-29451, -29697, -29934, -30162, -30381, -30591, -30791, -30982, -31163, -31335, -31498,
	-31650, -31794, -31927, -32051, -32165, -32269, -32364, -32448, -32523, -32587, -32642,
	-32687, -32722, -32747, -32762, -32767,
};

#define Q15 32767

/** @brief cos of @p mddeg (clamped to 0..180 degrees), Q15. */
static int32_t cos_q15(int32_t mddeg)
{
	int32_t deg, frac;

	if (mddeg < 0) {
		mddeg = -mddeg; /* cos is even. */
	}
	if (mddeg >= 180000) {
		return -Q15;
	}
	deg = mddeg / 1000;
	frac = mddeg % 1000;
	/* Linear interpolation between whole degrees; the second derivative of
	 * cos is bounded by 1, so the error over a 1-degree step is under
	 * 1.5e-4 rad-equivalent -- far below the jitter this ever sees. */
	return k_cos_q15[deg] + ((int32_t)(k_cos_q15[deg + 1] - k_cos_q15[deg]) * frac) / 1000;
}

/** @brief sin of @p mddeg via the same table, Q15. Valid for 0..180 degrees. */
static int32_t sin_q15(int32_t mddeg)
{
	/* sin(x) = cos(90 - x), and cos_q15 folds the negative half for us. */
	return cos_q15(90000 - mddeg);
}

/**
 * @brief Arccos of a Q15 value, in millidegrees.
 *
 * Binary search over the table, which is monotone decreasing, then linear
 * interpolation inside the bracketing degree.
 */
static int32_t acos_mddeg(int32_t q15)
{
	int lo = 0, hi = 180;
	int32_t span;

	if (q15 >= Q15) {
		return 0;
	}
	if (q15 <= -Q15) {
		return 180000;
	}
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;

		if (k_cos_q15[mid] > q15) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	/* k_cos_q15[lo] >= q15 > k_cos_q15[hi], hi == lo + 1. */
	span = k_cos_q15[lo] - k_cos_q15[hi];
	if (span <= 0) {
		return lo * 1000;
	}
	return lo * 1000 + ((k_cos_q15[lo] - q15) * 1000) / span;
}

void ultrawidelock_door_defaults(struct ultrawidelock_door_thresholds *th)
{
	if (th == NULL) {
		return;
	}
	th->closed_enter_mddeg = 2000; /* 2 deg */
	th->closed_leave_mddeg = 5000; /* 5 deg */
	th->open_enter_mddeg = 35000;  /* 35 deg */
	th->open_leave_mddeg = 30000;  /* 30 deg */
	th->dwell = 3;
}

/** @brief Is this geometry capable of producing an angle at all? */
static bool cfg_ok(const struct ultrawidelock_door_cfg *cfg)
{
	return cfg != NULL && cfg->hinge_to_frame_mm > 0 && cfg->hinge_to_leaf_mm > 0 &&
	       cfg->offset_mddeg >= 0 && cfg->offset_mddeg < 180000;
}

int32_t ultrawidelock_door_angle_mddeg(const struct ultrawidelock_door_cfg *cfg, int32_t d_mm)
{
	int64_t a, b, num, den;
	int32_t ratio, phi;

	if (!cfg_ok(cfg) || d_mm < 0) {
		return ULTRAWIDELOCK_DOOR_ANGLE_INVALID;
	}
	a = cfg->hinge_to_frame_mm;
	b = cfg->hinge_to_leaf_mm;

	/* cos(phi) = (a^2 + b^2 - d^2) / 2ab. Both sides scaled to Q15; the
	 * numerator reaches ~2e6 for a metre-scale door, so << 15 needs 64 bits. */
	num = (a * a + b * b - (int64_t)d_mm * d_mm) * Q15;
	den = 2 * a * b;
	if (den == 0) {
		return ULTRAWIDELOCK_DOOR_ANGLE_INVALID;
	}
	ratio = (int32_t)(num / den);

	/*
	 * A distance the hinge cannot explain is a measurement fault, not a very
	 * open door, and it must not be clamped into a plausible angle. The
	 * geometry can only produce d in [|a-b|, a+b], which is exactly
	 * |cos(phi)| <= 1; anything past that is rejected rather than saturated.
	 * One Q15 count of slack absorbs the integer division.
	 */
	if (ratio > Q15 + 1 || ratio < -Q15 - 1) {
		return ULTRAWIDELOCK_DOOR_ANGLE_INVALID;
	}

	phi = acos_mddeg(ratio);
	/* Reported from SHUT, not from the hinge's own zero. */
	return phi - cfg->offset_mddeg;
}

int32_t ultrawidelock_door_resolution_mddeg(const struct ultrawidelock_door_cfg *cfg,
					    int32_t at_mddeg, int32_t jitter_mm)
{
	int64_t a, b, d2, d, sens_num;
	int32_t phi, s;

	if (!cfg_ok(cfg) || jitter_mm <= 0) {
		return INT32_MAX;
	}
	a = cfg->hinge_to_frame_mm;
	b = cfg->hinge_to_leaf_mm;
	phi = at_mddeg + cfg->offset_mddeg;
	if (phi < 0 || phi > 180000) {
		return INT32_MAX;
	}

	/* d^2 = a^2 + b^2 - 2ab cos(phi), with cos in Q15. */
	d2 = a * a + b * b - (2 * a * b * cos_q15(phi)) / Q15;
	if (d2 <= 0) {
		return INT32_MAX;
	}
	/* Integer square root, Newton. d is at most a+b, so this converges in a
	 * handful of steps from that as the seed. */
	d = a + b;
	while (d > 0) {
		int64_t next = (d + d2 / d) / 2;

		if (next >= d) {
			break;
		}
		d = next;
	}
	if (d == 0) {
		return INT32_MAX;
	}

	/* dd/dphi = a*b*sin(phi)/d, in mm per radian. */
	s = (int32_t)sin_q15(phi);
	if (s <= 0) {
		return INT32_MAX; /* blind: the distance is stationary in the angle here. */
	}
	sens_num = (a * b * s) / Q15 / d;
	if (sens_num <= 0) {
		return INT32_MAX;
	}
	/* jitter_mm / (mm per radian) = radians; to millidegrees, x 180000/pi.
	 * 57295780 / 1000 is 180000/pi to seven figures. */
	return (int32_t)(((int64_t)jitter_mm * 57296) / sens_num);
}

bool ultrawidelock_door_init(struct ultrawidelock_door *d, const struct ultrawidelock_door_cfg *cfg,
		   const struct ultrawidelock_door_thresholds *th)
{
	if (d == NULL || !cfg_ok(cfg)) {
		return false;
	}
	d->cfg = *cfg;
	if (th != NULL) {
		d->th = *th;
	} else {
		ultrawidelock_door_defaults(&d->th);
	}
	if (d->th.dwell == 0u) {
		d->th.dwell = 1u;
	}
	d->state = ULTRAWIDELOCK_DOOR_UNKNOWN;
	d->cand = ULTRAWIDELOCK_DOOR_UNKNOWN;
	d->cand_n = 0u;
	d->last_mddeg = ULTRAWIDELOCK_DOOR_ANGLE_INVALID;
	return true;
}

/**
 * @brief Which state one angle argues for, given where we already are.
 *
 * The hysteresis lives here: leaving a state needs a bigger move than entering
 * it did, so a door resting on a threshold does not oscillate.
 */
static enum ultrawidelock_door_state classify(const struct ultrawidelock_door *d, int32_t mddeg)
{
	switch (d->state) {
	case ULTRAWIDELOCK_DOOR_CLOSED:
		if (mddeg <= d->th.closed_leave_mddeg) {
			return ULTRAWIDELOCK_DOOR_CLOSED;
		}
		break;
	case ULTRAWIDELOCK_DOOR_OPEN:
		if (mddeg >= d->th.open_leave_mddeg) {
			return ULTRAWIDELOCK_DOOR_OPEN;
		}
		break;
	default:
		break;
	}
	if (mddeg <= d->th.closed_enter_mddeg) {
		return ULTRAWIDELOCK_DOOR_CLOSED;
	}
	if (mddeg >= d->th.open_enter_mddeg) {
		return ULTRAWIDELOCK_DOOR_OPEN;
	}
	return ULTRAWIDELOCK_DOOR_AJAR;
}

enum ultrawidelock_door_state ultrawidelock_door_feed(struct ultrawidelock_door *d, int32_t d_mm)
{
	int32_t mddeg;
	enum ultrawidelock_door_state want;

	if (d == NULL) {
		return ULTRAWIDELOCK_DOOR_UNKNOWN;
	}
	mddeg = ultrawidelock_door_angle_mddeg(&d->cfg, d_mm);
	d->last_mddeg = mddeg;
	if (mddeg == ULTRAWIDELOCK_DOOR_ANGLE_INVALID) {
		/*
		 * A reading the geometry cannot explain breaks the dwell run but
		 * does NOT drop the committed state. One bad frame should not
		 * announce that a closed door is now unknown; only a run of
		 * agreeing good readings changes what this reports.
		 */
		d->cand = ULTRAWIDELOCK_DOOR_UNKNOWN;
		d->cand_n = 0u;
		return d->state;
	}

	want = classify(d, mddeg);
	if (want == d->state) {
		d->cand = ULTRAWIDELOCK_DOOR_UNKNOWN;
		d->cand_n = 0u;
		return d->state;
	}
	if (want == d->cand) {
		d->cand_n++;
	} else {
		d->cand = want;
		d->cand_n = 1u;
	}
	if (d->cand_n >= d->th.dwell) {
		d->state = want;
		d->cand = ULTRAWIDELOCK_DOOR_UNKNOWN;
		d->cand_n = 0u;
	}
	return d->state;
}
