/**
 * @file woz_ml.h — on-device classifiers, and the seam that keeps them replaceable.
 *
 * One classifier so far: line-of-sight vs obstructed, from the DW3000's own
 * receive diagnostics. It answers "is there a door between the reader and the
 * phone", which the ranging distance alone cannot: an obstructed 1 m and a clear
 * 3 m look similar to a time-of-flight estimate and do not mean the same thing.
 *
 * WHAT THIS IS NOT. It is not wired into the ranging path, and nothing calls it
 * yet. It is compiled, certified against its training-side model on every build
 * of the host suite, and sized. Wiring it to a decision is a separate change
 * that has to answer what a wrong answer costs.
 *
 * THE MODEL IS A DECISION TREE ON PURPOSE. Two integer comparisons; 776 B of
 * flash for everything here -- feature extraction, classification, the range
 * correction and the drift monitor -- 0 B of RAM, 28 B of stack. That is 1.4% of
 * the shipping image's free flash. No arena, no interpreter, no dynamic
 * allocation, nothing to
 * fail at init, and not one undefined symbol -- `arm-none-eabi-nm -u` on the
 * objects is empty, so the measured size is the whole size. It was measured
 * against an int8 TFLM network and a random forest and beat both on accuracy per
 * byte. It is also the only one of the three whose C can be proved identical to
 * the model that was trained -- gen_model.py (tinyml repo) enforces four gates
 * on every regeneration, and tests/host/test_woz_ml.c carries the claim into
 * CI.
 *
 * FEED IT THE RIGHT NUMBERS. woz_ml_los_classify() takes raw features in
 * physical units, in the order enum woz_ml_los_feature declares, and getting
 * that array wrong cannot be detected -- every float is a legal feature value.
 * So do not fill it by hand: woz_ml_los_features() below takes the five CIA
 * registers and the range and fills it, reproducing parse_alab.py register for
 * register, and gate 4 in gen_model.py holds it to that on every captured
 * reception.
 */
#ifndef WOZ_ML_H
#define WOZ_ML_H

#include <stdbool.h>
#include <stdint.h>

#include "woz_ml_los_features.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Channel condition between the reader and the phone. */
enum woz_ml_los_class {
	/** Direct path present: the strongest arrival is the first arrival. */
	WOZ_ML_LOS_CLEAR = 0,
	/** Something is in the way; the first path is attenuated or late. */
	WOZ_ML_LOS_OBSTRUCTED = 1,
};

/**
 * Classify one ranging exchange from its receive diagnostics.
 *
 * @param feat  WOZ_ML_LOS_N_FEATURES values in physical units, indexed by
 *              enum woz_ml_los_feature. Not checked and cannot be: every
 *              float is a legal feature value, so a mis-ordered array is
 *              indistinguishable from a real measurement.
 * @return enum woz_ml_los_class.
 *
 * Deterministic, reentrant, allocation-free, and bounded at two comparisons
 * deep, so it is safe to call from the ranging path. It still should not be
 * called from the RX arm window: that has a ~1836 us deadline and this has no
 * business competing for it.
 */
enum woz_ml_los_class woz_ml_los_classify(const float feat[WOZ_ML_LOS_N_FEATURES]);

/**
 * How much to trust the class woz_ml_los_classify() just returned. Never a class.
 *
 * A single tree's leaf IS its class, so the generated predict_proba writes 1.0
 * and 0.0 and says nothing. This is the distance from a linear boundary fitted
 * on the same two features, and it was measured to grade the tree's answers.
 * Binned by quartile on 544 captures, out-of-fold, the tree is right:
 *
 *     lowest quartile   0.7721
 *                       0.8162
 *                       0.9338
 *     highest quartile  0.9853
 *
 * The obvious cheaper candidate does not work and was measured: per-leaf
 * training purity, which is what emlearn emits at leaf_bits=8 for no multiplies
 * at all, is NOT monotone (0.7890, 0.9389, 0.8514, 0.9103). See RESULTS.md
 * Result 16.
 *
 * @param feat  the same vector woz_ml_los_classify() was given.
 * @return an unsigned confidence in dB-scaled units. Larger is more trustworthy.
 *         Zero means the reception sits on the boundary. There is no upper bound
 *         and no calibration to a probability; it is an ordering, so use it by
 *         comparing against a threshold chosen from the table above, never by
 *         reading it as a percentage.
 *
 * DELIBERATELY UNSIGNED, and this is the one thing to know before extending it.
 * The sign would be the linear model's own class, and that classifier is WORSE
 * than the tree: 0.8222 against 0.8773 on the same folds. Returning it would
 * hand every caller a five-point regression that looks like a free upgrade.
 * The two disagree on 9.7% of receptions, which would make a second unlabelled
 * drift monitor beside woz_ml_los_disagrees(); exposing the sign for that is a
 * two-line change, and it should happen when something counts it, not before.
 *
 * Costs no RAM and no libc, like the rest of this module.
 */
float woz_ml_los_confidence(const float feat[WOZ_ML_LOS_N_FEATURES]);

/**
 * The vendor rule of thumb, kept as the floor the model has to beat.
 *
 * Decawave APS006 says a first-path-to-total power difference above 6 dB implies
 * a non-line-of-sight channel. Refit on this board's own captures it scores
 * 0.7431 against the tree's 0.8800, so it is not a serious classifier -- it is a
 * reference. Two uses: it costs nothing to evaluate beside the tree, and the
 * RATE at which the two disagree is a drift signal that needs no ground truth.
 * A disagreement rate that moves means the install changed or the model went
 * stale, and that is the cheapest monitoring available for a shipped model.
 *
 * @param pwr_diff_db  rx_pwr - fp_pwr, in dB, as returned by
 *                     woz_ml_los_features(). The model itself no longer carries
 *                     this column, which is why it is passed rather than read
 *                     out of the feature vector.
 */
enum woz_ml_los_class woz_ml_los_vendor(float pwr_diff_db);

/**
 * True when the two above disagree. See woz_ml_los_vendor() for why to count it.
 *
 * @param pwr_diff_db  passed separately because the model no longer carries it:
 *                     rx_pwr - fp_pwr, both from the same dwt_rxdiag_t.
 */
bool woz_ml_los_disagrees(const float feat[WOZ_ML_LOS_N_FEATURES], float pwr_diff_db);

/**
 * Free-space spreading at a measured range, in dB, referenced to one metre.
 *
 * 20*log10(dist_cm / 100), computed from the integer's leading-bit position and a
 * nine-entry table rather than log10f, so this module keeps having no undefined
 * symbols at all. Maximum error against float64 is 0.183 dB over the range a
 * reader reports, and that error is dominated by range arriving as a whole number
 * of centimetres rather than by the table: see woz_ml_range.c.
 *
 * @param dist_cm  ranging distance in centimetres, clamped to [10, 2000]. Below
 *                 10 the correction diverges and the reader really does report
 *                 0 cm; above 2000 the curve is flat enough not to matter.
 */
float woz_ml_los_range_correction(uint16_t dist_cm);

/**
 * First-path power with the range taken out of it, which is the model's strongest
 * feature and the one at index WOZ_ML_LOS_F_FP_RESID.
 *
 * Equivalent to fp_pwr_db + woz_ml_los_range_correction(dist_cm), and provided as
 * one call because the two are only meaningful together.
 *
 * THE RANGE MAY LAG. A DS-TWR round yields its distance when the round completes,
 * while the diagnostics are read per reception, so a caller classifying every
 * reception has the previous round's range. That is what the training data
 * measured too, since the captured range came from a status line printed every
 * ~2 s, so a lagged range is the case the numbers in RESULTS.md Result 11 are
 * about rather than an approximation of it.
 */
float woz_ml_los_fp_resid(float fp_pwr_db, uint16_t dist_cm);

/**
 * Recover the true range from a reported one, given this reception's channel class.
 *
 * TWO MEASURED CONSTANTS, both from RESULTS.md Result 19, on this board in one
 * room:
 *
 *     true_cm ~= reported_cm + 25.5 - (obstructed ? 84.5 : 0)
 *
 * The 25.5 cm is the reader's own fixed shortfall. Nothing in this project has
 * ever programmed the DW3000 antenna-delay registers, so every range it reports
 * is uncalibrated in the absolute sense; two clear runs at 100 cm and 200 cm
 * measured -25.0 and -26.0 cm, agreeing to a centimetre across a doubling, which
 * is an offset rather than a scale error. The 84.5 cm is what a body in the path
 * adds, measured with the phone on a tripod so that only the subject moved.
 *
 * Fitted against all four runs the two constants predict 74.5/159.0/174.5/259.0
 * against measured 75/155/174/263: worst error 4 cm over a 2:1 range span.
 *
 * WHY THIS ONLY EVER ADDS PERMISSION, which is the property that makes it safe to
 * apply to a lock. Obstruction inflates range and never shrinks it, so undoing it
 * cannot manufacture proximity the radio did not see. The check that defends
 * against a deliberate distance-reduction attack is STS quality, and nothing
 * here touches it.
 *
 * FEED IT A FILTERED RANGE, NOT ONE RECEPTION. The obstructed captures had
 * interquartile spreads of 38 and 47.5 cm against 7 and 11.5 for clear, so a
 * single obstructed reception is a poor estimate of anything and this correction
 * does not make it a better one. aliro_approach already takes a median over its
 * window; that is the quantity to correct.
 *
 * @param reported_cm  the range as the reader produced it.
 * @param c            the class woz_ml_los_classify() returned for the reception
 *                     the range came from. Pass WOZ_ML_LOS_CLEAR to apply the
 *                     antenna offset alone.
 * @return the corrected range in centimetres, clamped at 0. An obstructed
 *         reception at very short range can correct below zero, and that is a
 *         reading the correction had no business being applied to rather than a
 *         negative distance.
 *
 * ONE ROOM, ONE PHONE, ONE SUBJECT, ONE SESSION, and a body is not a door. The
 * 25.5 cm in particular is this board and this antenna rather than a family
 * constant, and antenna delay drifts with temperature in ways nothing here
 * measured.
 */
uint16_t woz_ml_los_range_true_cm(uint16_t reported_cm, enum woz_ml_los_class c);

/**
 * The Ipatov CIA diagnostics the features are computed from, as read.
 *
 * A struct rather than five uint32_t parameters precisely because they are five
 * same-typed integers: positional arguments there are a silent mis-order waiting
 * to happen, and like the feature vector itself a mis-ordered read produces a
 * confident wrong class rather than an error. Field for field from
 * dwt_rxdiag_t, so a caller writes .f1 = d.ipatovF1 and can check it by eye.
 *
 * woz_ml deliberately does not include deca_device_api.h. That header is
 * LicenseRef-QORVO-2 and is only present on targets that carry the driver, while
 * this module is built and tested on the host with no driver at all.
 */
struct woz_ml_cia {
	uint32_t f1;           /**< dwt_rxdiag_t::ipatovF1 */
	uint32_t f2;           /**< dwt_rxdiag_t::ipatovF2 */
	uint32_t f3;           /**< dwt_rxdiag_t::ipatovF3 */
	uint16_t accum_count;  /**< dwt_rxdiag_t::ipatovAccumCount */
	uint32_t channel_area; /**< dwt_rxdiag_t::ipatovPower, a 2^17-scaled area */
};

/**
 * Fill a feature vector from one reception's diagnostics and the current range.
 *
 * @param cia          the Ipatov registers, as read.
 * @param dist_cm      ranging distance in centimetres. May lag by one round; see
 *                     woz_ml_los_fp_resid() for why that is the trained case
 *                     rather than an approximation of it.
 * @param feat         filled on success, ready for woz_ml_los_classify().
 * @param pwr_diff_db  rx_pwr - fp_pwr, for woz_ml_los_vendor() and the
 *                     disagreement counter. NULL if not wanted.
 * @return false when the CIA read failed, in which case nothing is written and
 *         the reception must be DROPPED rather than classified. A zeroed
 *         accumulator count or channel area is a failed read, not a very weak
 *         channel, and the training data drops those receptions too.
 *
 * Allocation-free and reentrant, like everything else here. Costs one 64-bit
 * multiply-accumulate and three logarithms.
 */
bool woz_ml_los_features(const struct woz_ml_cia *cia, uint16_t dist_cm,
			 float feat[WOZ_ML_LOS_N_FEATURES], float *pwr_diff_db);

#ifdef __cplusplus
}
#endif

#endif /* WOZ_ML_H */
