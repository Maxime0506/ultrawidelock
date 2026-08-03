/**
 * @file aliro_approach.c
 * Kalman-filtered approach controller for predictive unlock. Tracks distance (cm), velocity (cm/s),
 * and estimated time-to-arrival (ms) at the unlock radius. Supervises presence via median filtering
 * of trusted ranges and fires predictive unlock when closing speed and ETA meet thresholds. Factory
 * defaults: unlock 100 cm, relock 250 cm, dwell times 2 s and 3 s, motor delay 500 ms, margin 250
 * ms, velocity floor 30 cm/s, prediction enabled.
 */
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_approach — predictive approach controller. See aliro_approach.h for
 * the model; this file holds the estimator tuning.
 */
#include "aliro_approach.h"

#include <string.h>

/* Kalman tuning. Measurement noise from bench range scatter (~30 cm sigma on
 * normal blocks; the metre-scale outliers are gated, not modelled). Process
 * noise from human gait acceleration (~1.5 m/s^2). */
#define KF_R_CM2       900.0f   /* (30 cm)^2 */
#define KF_QA          22500.0f /* (150 cm/s^2)^2 */
#define KF_GATE_CM     100.0f   /* innovation gate; > max per-block walk step */
#define KF_REJECT_MAX  3        /* consecutive rejects -> re-base on the data */
#define KF_STALE_MS    1000     /* sample gap that re-bases instead of coasting */
#define KF_VINIT_CM2S2 40000.0f /* (200 cm/s)^2 initial velocity variance */
#define KF_MIN_SAMPLES 6        /* accepted samples before predictions arm */

/* Prediction guards: two consecutive in-window samples to fire; a predictive
 * open must convert to presence within ETA + grace (grace covers the median
 * filter's lag into the unlock band) or it aborts. */
#define PRED_DWELL_N  2
#define PRED_GRACE_MS 900

/**
 * Reset the Kalman filter to a new state given a fresh measurement: clears rejection history,
 * zeroes velocity, reinitializes covariance, and resets prediction state.
 */
static void kf_rebase(struct aliro_approach *ap, int64_t now_ms, int32_t cm)
{
	ap->kf_init = true;
	ap->accepted = 1;
	ap->rejects = 0;
	ap->last_ms = now_ms;
	ap->d = (float)cm;
	ap->v = 0.0f;
	ap->p00 = KF_R_CM2;
	ap->p01 = 0.0f;
	ap->p11 = KF_VINIT_CM2S2;
	ap->pred_dwell = 0;
	ap->eta_ms = -1;
}

/* Time update always (time really passed), measurement update only inside the
 * innovation gate. Returns true when the sample updated the estimate. */
static bool kf_update(struct aliro_approach *ap, int64_t now_ms, int32_t cm)
{
	if (!ap->kf_init || (now_ms - ap->last_ms) > KF_STALE_MS) {
		kf_rebase(ap, now_ms, cm);
		return true;
	}

	float dt = (float)(now_ms - ap->last_ms) / 1000.0f;

	if (dt < 0.001f) {
		dt = 0.001f;
	}
	ap->last_ms = now_ms;

	/* predict: x = F x, P = F P F' + Q (constant-velocity F, accel Q) */
	ap->d += ap->v * dt;
	float dt2 = dt * dt;

	ap->p00 += dt * (ap->p01 + ap->p01) + dt2 * ap->p11 + KF_QA * dt2 * dt2 / 4.0f;
	ap->p01 += dt * ap->p11 + KF_QA * dt2 * dt / 2.0f;
	ap->p11 += KF_QA * dt2;

	float y = (float)cm - ap->d;
	float ay = y < 0.0f ? -y : y;

	if (ay > KF_GATE_CM) {
		/* Metre-scale spike: skip the measurement update. A run of
		 * agreeing "spikes" is real data (a gap, a re-appearance) —
		 * re-base on it rather than wedging the gate shut. */
		if (++ap->rejects >= KF_REJECT_MAX) {
			kf_rebase(ap, now_ms, cm);
			return true;
		}
		return false;
	}
	ap->rejects = 0;

	float s = ap->p00 + KF_R_CM2;
	float k0 = ap->p00 / s;
	float k1 = ap->p01 / s;

	ap->d += k0 * y;
	ap->v += k1 * y;
	ap->p11 -= k1 * ap->p01;
	ap->p01 -= k0 * ap->p01;
	ap->p00 -= k0 * ap->p00;
	if (ap->accepted < KF_MIN_SAMPLES) {
		ap->accepted++;
	}
	return true;
}

/* Median of the first n samples of win (n in [1, ALIRO_APPROACH_MEDIAN_N]).
 * Rejects the metre-scale spikes in the per-block UWB distance without the
 * lag of a running average. */
static int32_t range_median(const int32_t *win, int n)
{
	int32_t t[ALIRO_APPROACH_MEDIAN_N];

	for (int i = 0; i < n; i++) {
		t[i] = win[i];
	}
	for (int i = 1; i < n; i++) { /* insertion sort; n <= 5 */
		int32_t v = t[i];
		int j = i - 1;

		while (j >= 0 && t[j] > v) {
			t[j + 1] = t[j];
			j--;
		}
		t[j + 1] = v;
	}
	return t[n / 2];
}

/**
 * Initialize an approach configuration with factory defaults: unlock at 100 cm, relock at 250 cm,
 * dwell times 2 s and 3 s, motor delay 500 ms, margin 250 ms, velocity floor 30 cm/s, predictive
 * unlock enabled.
 */
void aliro_approach_defaults(struct aliro_approach_cfg *cfg)
{
	cfg->unlock_cm = 100;
	cfg->relock_cm = 250;
	cfg->near_dwell = 2;
	cfg->far_dwell = 3;
	cfg->motor_ms = 500;
	cfg->margin_ms = 250;
	cfg->vmin_cm_s = 30;
	/*
	 * 750 ms, and the number is measured rather than chosen. A ranging block
	 * is 192 ms, so this is four missed blocks -- a phone that is still there
	 * and still ranging cannot be quiet that long by accident.
	 *
	 * It was 1,500 ms, which was too conservative and cost a relock on
	 * 2026-08-02 17:40 by 500 ms: the last far sample was fed 1,000 ms before
	 * the session ended, so only ~870 ms of silence had accumulated when the
	 * link dropped. The walk-away that DID work the same afternoon had 1.2 s.
	 * Real departures land either side of 1,500 ms and reliably above 750.
	 *
	 * Erring short is the safe direction here, and only here: this rule fires
	 * only when the last measurement was already beyond relock_cm, so being
	 * early can relock a door the credential has left and can never relock one
	 * it is standing at.
	 */
	cfg->far_silence_ms = 750;
	cfg->predict_en = true;
}

/**
 * Initialize an approach controller to locked state with zero velocity and no prediction in flight.
 * If cfg is NULL, load factory defaults; otherwise copy the provided configuration.
 */
void aliro_approach_init(struct aliro_approach *ap, const struct aliro_approach_cfg *cfg)
{
	memset(ap, 0, sizeof(*ap));
	if (cfg != NULL) {
		ap->cfg = *cfg;
	} else {
		aliro_approach_defaults(&ap->cfg);
	}
	ap->locked = true;
	ap->eta_ms = -1;
}

/**
 * Record a predictive unlock abort and relock the door. Called when prediction is active but the
 * phone has stopped approaching or moved away.
 */
static enum aliro_approach_action pred_abort(struct aliro_approach *ap)
{
	ap->locked = true;
	ap->pred_open = false;
	ap->pred_dwell = 0;
	return ALIRO_APPROACH_RELOCK_ABORT;
}

/**
 * Update the Kalman filter state with a new range measurement, compute estimated time-to-arrival
 * (ETA) at the unlock radius, track presence via a median-filter window, and supervise predictive
 * unlock (fire early when closing speed and ETA permit, abort if the phone stops or moves away).
 * Return the action code: UNLOCK_THRESHOLD (entered unlock zone), RELOCK_DEPART (exited relock
 * zone), UNLOCK_PREDICT (fired a predictive unlock), or HOLD (no action).
 */
enum aliro_approach_action aliro_approach_feed(struct aliro_approach *ap, int64_t now_ms,
					       int32_t cm)
{
	bool est_fresh = kf_update(ap, now_ms, cm);

	/* ETA to the unlock radius, kept current for the trace accessors and
	 * the fire decision below. Valid only with a converged filter, a real
	 * closing speed and the credential still outside the radius. */
	float closing = -ap->v;

	if (ap->cfg.predict_en && est_fresh && ap->accepted >= KF_MIN_SAMPLES &&
	    closing >= (float)ap->cfg.vmin_cm_s && ap->d > (float)ap->cfg.unlock_cm) {
		ap->eta_ms = (int32_t)(((ap->d - (float)ap->cfg.unlock_cm) * 1000.0f) / closing);
	} else {
		ap->eta_ms = -1;
		ap->pred_dwell = 0;
	}

	/* Presence path — the shipped median/dwell controller, verbatim: every
	 * trusted sample enters the window (the median itself rejects spikes),
	 * dwell counters reset across the dead band. */
	ap->last_cm = cm;
	ap->last_feed_ms = now_ms;

	ap->win[ap->wpos] = cm;
	ap->wpos = (ap->wpos + 1) % ALIRO_APPROACH_MEDIAN_N;
	if (ap->wlen < ALIRO_APPROACH_MEDIAN_N) {
		ap->wlen++;
	}
	int32_t f = range_median(ap->win, ap->wlen);

	if (f <= ap->cfg.unlock_cm) {
		ap->far_dwell = 0;
		if (ap->pred_open) {
			/* Arrived: the predictive open converts to a normal
			 * presence unlock; departure rules take over. */
			ap->pred_open = false;
		}
		if (ap->locked && ++ap->near_dwell >= ap->cfg.near_dwell) {
			ap->locked = false;
			return ALIRO_APPROACH_UNLOCK_THRESHOLD;
		}
	} else if (f >= ap->cfg.relock_cm) {
		ap->near_dwell = 0;
		if (!ap->locked && ++ap->far_dwell >= ap->cfg.far_dwell) {
			ap->locked = true;
			ap->pred_open = false;
			ap->pred_dwell = 0;
			return ALIRO_APPROACH_RELOCK_DEPART;
		}
	} else {
		ap->near_dwell = 0;
		ap->far_dwell = 0;
	}

	/* Prediction path. Supervise an open first: it must keep closing and
	 * arrive on time, or the bolt goes back. */
	if (ap->pred_open) {
		if (now_ms >= ap->pred_deadline_ms) {
			return pred_abort(ap);
		}
		if (est_fresh && closing < (float)ap->cfg.vmin_cm_s / 2.0f) {
			return pred_abort(ap);
		}
		return ALIRO_APPROACH_HOLD;
	}

	if (ap->locked && ap->eta_ms >= 0 && ap->eta_ms <= ap->cfg.motor_ms + ap->cfg.margin_ms) {
		if (++ap->pred_dwell >= PRED_DWELL_N) {
			ap->locked = false;
			ap->pred_open = true;
			ap->pred_dwell = 0;
			ap->pred_deadline_ms = now_ms + ap->eta_ms + PRED_GRACE_MS;
			return ALIRO_APPROACH_UNLOCK_PREDICT;
		}
	} else {
		ap->pred_dwell = 0;
	}
	return ALIRO_APPROACH_HOLD;
}

/**
 * Supervise an active predictive unlock when no new measurement arrives this window. If the
 * prediction deadline has passed, abort and relock the door. Return HOLD otherwise.
 */
void aliro_approach_observe_departure(struct aliro_approach *ap, int64_t now_ms, int32_t cm)
{
	if (ap == NULL || cm < ap->cfg.relock_cm) {
		return;
	}
	/*
	 * Only these two, and deliberately: no median window, no dwell counter,
	 * no filter update. An unvouched range must be able to start the
	 * departure clock and must not be able to touch anything an unlock
	 * decision reads.
	 */
	ap->last_cm = cm;
	ap->last_feed_ms = now_ms;
}

/**
 * Advance the approach state machine by one tick: handle predictive unlock abort on deadline,
 * departure by silence when measurements stop after the phone leaves the relock threshold, and
 * return the triggered action or HOLD if no action occurred.
 */
enum aliro_approach_action aliro_approach_tick(struct aliro_approach *ap, int64_t now_ms)
{
	/* No sample this window: the estimate is frozen (no coasting — a
	 * measurement-free filter would happily "arrive" on its own). Only an
	 * overdue predictive open acts here. */
	if (ap->pred_open && now_ms >= ap->pred_deadline_ms) {
		return pred_abort(ap);
	}

	/*
	 * Departure by silence, which is how a real walk-away ends: the far
	 * samples stop before far_dwell can count three of them. Gated on the
	 * LAST measurement being beyond the relock radius, so a phone resting
	 * near the door goes quiet without the bolt moving under its owner.
	 * See aliro_approach_cfg::far_silence_ms.
	 */
	if (!ap->locked && ap->cfg.far_silence_ms > 0 && ap->last_feed_ms != 0 &&
	    ap->last_cm >= ap->cfg.relock_cm &&
	    (now_ms - ap->last_feed_ms) >= (int64_t)ap->cfg.far_silence_ms) {
		ap->locked = true;
		ap->near_dwell = 0;
		ap->far_dwell = 0;
		ap->pred_open = false;
		ap->pred_dwell = 0;
		return ALIRO_APPROACH_RELOCK_DEPART;
	}
	return ALIRO_APPROACH_HOLD;
}

/**
 * Reset the approach controller to locked state while preserving its configuration. Return
 * RELOCK_DEPART if the door was unlocked before the reset, otherwise HOLD.
 */
enum aliro_approach_action aliro_approach_gone(struct aliro_approach *ap)
{
	bool was_open = !ap->locked;
	struct aliro_approach_cfg cfg = ap->cfg;

	aliro_approach_init(ap, &cfg);
	return was_open ? ALIRO_APPROACH_RELOCK_DEPART : ALIRO_APPROACH_HOLD;
}

/**
 * Return true if the door is locked, false if unlocked.
 */
bool aliro_approach_locked(const struct aliro_approach *ap)
{
	return ap->locked;
}

/**
 * Return the current estimated distance in centimeters. Returns -1 if the Kalman filter has not
 * been initialized (no valid measurement yet); otherwise returns the rounded estimate.
 */
int32_t aliro_approach_est_cm(const struct aliro_approach *ap)
{
	if (!ap->kf_init) {
		return -1;
	}
	return (int32_t)(ap->d + (ap->d < 0.0f ? -0.5f : 0.5f));
}

/**
 * Return the current velocity in centimeters per second (positive = approaching, negative =
 * receding). Returns 0 if the Kalman filter has not been initialized.
 */
int32_t aliro_approach_vel_cm_s(const struct aliro_approach *ap)
{
	if (!ap->kf_init) {
		return 0;
	}
	float c = -ap->v;

	return (int32_t)(c + (c < 0.0f ? -0.5f : 0.5f));
}

/**
 * Return the estimated time in milliseconds until approach completes (unlock reaches the door).
 * Value is -1 if not yet computed, or the reader has already locked the door.
 */
int32_t aliro_approach_eta_ms(const struct aliro_approach *ap)
{
	return ap->eta_ms;
}
