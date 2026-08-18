/* SPDX-License-Identifier: ISC */

/**
 * @file ultrawidelock_ml_los.c — the seam: scale into the model's space, then walk the tree.
 *
 * Everything model-specific is generated (ultrawidelock_ml_los_tree.h,
 * ultrawidelock_ml_los_model.h). What is written by hand is only the quantisation, and that is
 * written by hand because it is the one place the target can silently disagree with the bench.
 */

#include "ultrawidelock_ml.h"
#include "ultrawidelock_ml_los_scaler.h"
#include "ultrawidelock_ml_los_tree.h"

/* The tree was trained on features mapped affinely into int16:
 *
 *     q = (x - lo) * scale - 16000,  rounded to nearest, clamped
 *
 * so this is part of the model, not a preprocessing convenience. The constants
 * come from the training split's per-feature minimum and range.
 *
 * float32, not double. gen_model.py (tinyml repo) refuses to emit a model unless
 * the float32 result classifies identically to the float64 one sklearn trained
 * through, on all 6,300 held-out samples. At the last regeneration 58 of 88,200
 * quantised values landed one LSB apart between the two, and none of them
 * changed a class -- which is exactly the margin that gate exists to watch.
 */
#define Q_OFFSET 16000.0f
#define Q_MIN    (-32768)
#define Q_MAX    32767

static int16_t quantise(float x, float lo, float scale)
{
	/* Deliberately not lrintf(): it pulls in libm for a rounding mode that is
	 * not the one wanted anyway. Add-half-and-truncate is half away from zero,
	 * and gen_model.py's gate 2 simulates THIS expression in float32 rather
	 * than numpy's default half-to-even, so the gate is testing what runs
	 * here. If this line changes, quantise_f32() in the generator changes with
	 * it or the gate goes quietly hollow. */
	const float q = (x - lo) * scale - Q_OFFSET;
	const float r = (q >= 0.0f) ? (q + 0.5f) : (q - 0.5f);

	if (r <= (float)Q_MIN) {
		return (int16_t)Q_MIN;
	}
	if (r >= (float)Q_MAX) {
		return (int16_t)Q_MAX;
	}
	return (int16_t)r;
}

enum ultrawidelock_ml_los_class
ultrawidelock_ml_los_classify(const float feat[ULTRAWIDELOCK_ML_LOS_N_FEATURES])
{
	int16_t q[ULTRAWIDELOCK_ML_LOS_N_FEATURES];

	for (int i = 0; i < ULTRAWIDELOCK_ML_LOS_N_FEATURES; i++) {
		q[i] = quantise(feat[i], ultrawidelock_ml_los_lo[i], ultrawidelock_ml_los_scale[i]);
	}

	/* The generated predictor returns the class index directly. It cannot
	 * fail: every path through the tree ends in a leaf. */
	return (enum ultrawidelock_ml_los_class)ultrawidelock_ml_los_tree_predict(
		q, ULTRAWIDELOCK_ML_LOS_N_FEATURES);
}

/* Decawave APS006 section 3.5: a first-path-to-total receive power difference
 * greater than 6 dB implies a non-line-of-sight channel. Kept as the floor, not
 * as a fallback -- see the header for why the disagreement rate is the useful
 * part. */
#define VENDOR_THRESHOLD_DB 6.0f

enum ultrawidelock_ml_los_class ultrawidelock_ml_los_vendor(float pwr_diff_db)
{
	return (pwr_diff_db > VENDOR_THRESHOLD_DB) ? ULTRAWIDELOCK_ML_LOS_OBSTRUCTED
						   : ULTRAWIDELOCK_ML_LOS_CLEAR;
}

bool ultrawidelock_ml_los_disagrees(const float feat[ULTRAWIDELOCK_ML_LOS_N_FEATURES],
				    float pwr_diff_db)
{
	/* pwr_diff arrives separately rather than out of `feat` because the model no
	 * longer uses it: on this board's captures a threshold on pwr_diff scores
	 * 0.7431 against 0.8800 for the shipped pair, so it was dropped from the
	 * feature vector. The vendor rule still needs it, and the caller has it -- it
	 * is rx_pwr minus fp_pwr, both read from the same dwt_rxdiag_t. */
	return ultrawidelock_ml_los_classify(feat) != ultrawidelock_ml_los_vendor(pwr_diff_db);
}
