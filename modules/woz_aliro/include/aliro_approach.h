/**
 * @file aliro_approach.h
 * Configuration and state for approach detection and predictive unlock: unlock/relock thresholds in
 * centimeters, sample-count dwell times, motor retraction time, scheduling margin, minimum closing
 * speed, and a flag to enable or disable predictive ToA unlock.
 */
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_approach — predictive approach controller ("negative latency").
 *
 * Turns the per-block trusted UWB range stream into bolt decisions. Two paths
 * cooperate:
 *
 *   Presence (threshold) path — the shipped behaviour, unchanged: trusted
 *   ranges go through a median filter (rejects the metre-scale per-block
 *   spikes) and near/far dwell counters across a wide hysteresis band before
 *   the bolt moves. Slow shuffles and stand-at-door still unlock here.
 *
 *   Prediction path — a 1-D constant-velocity Kalman filter over the same
 *   samples yields distance + closing speed. When the estimated time of
 *   arrival at the unlock radius drops inside the bolt's retraction window
 *   (motor_ms + margin_ms), retraction is started early so it COMPLETES at
 *   arrival instead of beginning there. The invariant is "open only while an
 *   authenticated credential is closing on the door": a prediction needs a
 *   converged filter, a closing speed above vmin_cm_s, and two consecutive
 *   qualifying samples. A predictively opened bolt that has not yet arrived
 *   relocks the moment the approach stops (closing speed decays) or the
 *   arrival is overdue — a stationary credential outside the unlock radius
 *   never holds the door open.
 *
 * Pure logic, caller-allocated state, no platform dependencies: the same unit
 * runs on the target and in the host suite / web twin replays. Call from task
 * context only (uses float math; never from the UWB RX/ISR path).
 */
#ifndef ALIRO_APPROACH_H
#define ALIRO_APPROACH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spike-rejecting median window over trusted ranges (odd; samples). */
#define ALIRO_APPROACH_MEDIAN_N 5

/* One decision per fed sample / tick; the caller mirrors these onto the bolt.
 * UNLOCK/RELOCK values fire once per transition (the controller tracks the
 * bolt state), so acting on every non-HOLD return is idempotent-safe. */
enum aliro_approach_action {
	ALIRO_APPROACH_HOLD = 0,
	/* ETA inside the retraction window: start the bolt now so it finishes
	 * at arrival. */
	ALIRO_APPROACH_UNLOCK_PREDICT,
	/* Classic presence unlock: median inside unlock_cm for near_dwell
	 * consecutive samples. */
	ALIRO_APPROACH_UNLOCK_THRESHOLD,
	/* Departure: median beyond relock_cm for far_dwell consecutive
	 * samples (or the peer vanished, via aliro_approach_gone()). */
	ALIRO_APPROACH_RELOCK_DEPART,
	/* Predictive open aborted: the approach stopped/turned or arrival is
	 * overdue while still outside the unlock radius. */
	ALIRO_APPROACH_RELOCK_ABORT,
};

/**
 * Configuration for approach detection: unlock_cm (presence radius and ETA target), relock_cm
 * (departure threshold), near_dwell/far_dwell (sample counts to unlock/relock), motor_ms (bolt
 * retraction time), margin_ms (scheduling slack, >= 192 ms to avoid missing discrete samples),
 * vmin_cm_s (min closing speed to arm prediction), predict_en (false disables prediction and leaves
 * presence path unchanged; also false whenever RSSI power gate is active).
 */
struct aliro_approach_cfg {
	int32_t unlock_cm; /* presence radius; also the ETA target */
	int32_t relock_cm; /* departure radius (hysteresis band top) */
	int near_dwell;    /* consecutive medians <= unlock_cm to unlock */
	int far_dwell;     /* consecutive medians >= relock_cm to relock */
	int32_t motor_ms;  /* bolt retraction time for this lock model */
	int32_t margin_ms; /* scheduling slack on top of motor_ms; keep it
			    * >= one ranging block (192 ms) so the discrete
			    * sample grid cannot miss the window */
	int32_t vmin_cm_s; /* min closing speed for a prediction to fire */
	bool predict_en;   /* arm the prediction path at all; false leaves the
			    * presence path exactly as it shipped. Off whenever
			    * the RSSI power gate is on: the gate withholds
			    * ranging until the credential is already inside
			    * unlock_cm, so no ETA can ever arm and the two
			    * features would only pretend to cooperate. */
};

/**
 * State machine and Kalman filter for approach detection and predictive unlock.
 * locked: bolt mirror state. win/wlen/wpos: median filter for distance. near_dwell/far_dwell:
 * hysteresis counters. kf_init/accepted/rejects/last_ms/d/v/p00/p01/p11: constant-velocity Kalman
 * filter (distance cm, velocity cm/s; negative velocity = closing).
 * pred_dwell/pred_open/pred_deadline_ms/eta_ms: predictive unlock path (fires when closing speed >=
 * vmin_cm_s and ETA to unlock_cm is within motor_ms + margin_ms).
 */
struct aliro_approach {
	struct aliro_approach_cfg cfg;

	/* bolt mirror + presence path */
	bool locked;
	int32_t win[ALIRO_APPROACH_MEDIAN_N];
	int wlen, wpos;
	int near_dwell, far_dwell;

	/* constant-velocity Kalman filter (cm, cm/s; v < 0 = closing) */
	bool kf_init;
	int accepted; /* samples accepted since (re)init */
	int rejects;  /* consecutive innovation-gate rejects */
	int64_t last_ms;
	float d, v;
	float p00, p01, p11;

	/* prediction */
	int pred_dwell;           /* consecutive in-window samples */
	bool pred_open;           /* opened predictively, not yet arrived */
	int64_t pred_deadline_ms; /* arrive by this or RELOCK_ABORT */
	int32_t eta_ms;           /* last ETA to unlock_cm; -1 = none */
};

/* Fill cfg with the tuned defaults (100/250 cm band, 2/3 dwells, 500 ms
 * motor + 250 ms margin, 30 cm/s floor). */
void aliro_approach_defaults(struct aliro_approach_cfg *cfg);

/* cfg == NULL uses the defaults. Starts locked, idle. */
void aliro_approach_init(struct aliro_approach *ap, const struct aliro_approach_cfg *cfg);

/* One trusted range sample (task context, timestamps in ms, any monotonic
 * base). Runs both paths, returns at most one transition. */
enum aliro_approach_action aliro_approach_feed(struct aliro_approach *ap, int64_t now_ms,
					       int32_t cm);

/* Periodic call while no sample arrived (the controller's idle tick).
 * Only supervises an overdue predictive open. */
enum aliro_approach_action aliro_approach_tick(struct aliro_approach *ap, int64_t now_ms);

/* Peer gone (ranging silent past the caller's timeout): reset for the next
 * approach; returns RELOCK_DEPART if the bolt was open, else HOLD. */
enum aliro_approach_action aliro_approach_gone(struct aliro_approach *ap);

/* Trace accessors (for the ALAB walk-up report / twin overlays). */
bool aliro_approach_locked(const struct aliro_approach *ap);
int32_t aliro_approach_est_cm(const struct aliro_approach *ap);   /* -1 = none */
int32_t aliro_approach_vel_cm_s(const struct aliro_approach *ap); /* >0 = closing */
int32_t aliro_approach_eta_ms(const struct aliro_approach *ap);   /* -1 = none */

#ifdef __cplusplus
}
#endif

#endif /* ALIRO_APPROACH_H */
