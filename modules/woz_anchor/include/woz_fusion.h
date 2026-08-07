/**
 * @file woz_fusion.h — two-anchor fusion: which side of the door, and whether
 *       the two distances are geometrically possible at all.
 *
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage E of internal/two-anchor-plan.md. Two anchors a known distance apart
 * each measure a distance to the same phone. Two independent things fall out:
 *
 * 1. WHICH SIDE. The set of points equidistant from both anchors is the
 *    perpendicular bisector of the baseline, so the sign of (d_inside -
 *    d_outside) says which side of that plane the phone is on. This only means
 *    "inside or outside the door" if the anchors are mounted so that the
 *    bisector IS the door plane -- symmetrically about it, at the same height.
 *    That is a mounting requirement and no amount of code substitutes for it.
 *
 * 2. WHETHER TO BELIEVE IT. Anchor A, anchor B and the phone form a triangle
 *    whose third side is known and fixed, so the two measured sides must
 *    satisfy the triangle inequality.
 *
 * WHAT THE TRIANGLE GATE IS AND IS NOT. It catches ASYMMETRIC manipulation and
 * impossible pairs: one link reporting a distance too short to span the
 * baseline, one link inflated relative to the other, or two ranges that came
 * from different targets or different moments. That covers the realistic
 * distance-reduction attack, where an attacker compromises one link rather
 * than keeping two simultaneously consistent.
 *
 * It does NOT catch a symmetric relay -- one that inflates both links by the
 * same amount through a single point near the door. Both measured distances
 * grow together, the difference is unchanged, and the triangle still closes.
 * That case needs no gate: inflating a distance makes the phone look FURTHER,
 * which fails the unlock radius on its own. The attack that matters is
 * reduction, and what defends it is time-of-flight plus STS, not this.
 *
 * So this is a geometric consistency check layered ON TOP of the existing
 * per-link integrity consensus (docs/range-integrity.md), never instead of it.
 * Feed it only distances that already passed their own link's gate; a
 * consistency check over untrusted numbers is worse than none, because it
 * looks like evidence.
 */

#ifndef WOZ_FUSION_H
#define WOZ_FUSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Which side of the anchor baseline's perpendicular bisector the phone is on. */
enum woz_side {
	WOZ_SIDE_UNKNOWN = 0, /**< inside the dead band, or the pair was rejected. */
	WOZ_SIDE_INSIDE,
	WOZ_SIDE_OUTSIDE,
};

/** Fixed install geometry plus the two tolerances, both sized from measured jitter. */
struct woz_fusion_cfg {
	/** Anchor separation, mm. Known and fixed: measure it once, at install. */
	int32_t baseline_mm;
	/**
	 * Slack on the triangle inequality, mm. Size at 3 sigma of the measured
	 * per-link jitter, so a legitimate pair essentially never trips it --
	 * this gate exists to catch impossible geometry, not ordinary noise.
	 */
	int32_t tol_mm;
	/**
	 * Half-width of the UNKNOWN band about the bisector, mm. Size at about
	 * 2 sigma: within it the two distances are not far enough apart for
	 * their difference to have a reliable sign, and saying so beats
	 * guessing. A phone standing exactly in the doorway is genuinely
	 * ambiguous and should read that way.
	 */
	int32_t deadband_mm;
};

/** The verdict, with the evidence that produced it rather than just a yes or no. */
struct woz_fusion_verdict {
	enum woz_side side;
	/** False when the triangle inequality failed; @p side is then UNKNOWN. */
	bool geometry_ok;
	/** d_inside - d_outside, mm. Negative means nearer the inside anchor. */
	int32_t delta_mm;
};

/**
 * @brief Fuse one pair of distances taken from the same ranging round.
 *
 * @param cfg Install geometry and tolerances.
 * @param d_inside_mm Distance from the anchor on the inside of the door.
 * @param d_outside_mm Distance from the anchor on the outside.
 *
 * The two distances MUST be from the same round. Pairing samples taken at
 * different moments while the phone is moving produces a geometrically
 * impossible pair, which this will correctly reject -- and a stream of
 * rejections whose real cause is a pairing bug reads as a hardware fault.
 */
struct woz_fusion_verdict woz_fusion_eval(const struct woz_fusion_cfg *cfg, int32_t d_inside_mm,
					  int32_t d_outside_mm);

/**
 * @brief Whether a verdict may release a PREDICTIVE unlock.
 *
 * True for INSIDE and, deliberately, for UNKNOWN.
 *
 * UNKNOWN permits it because the alternative is worse. A satellite that has
 * gone quiet, a phone in the doorway, or a fresh boot all produce UNKNOWN, and
 * a door that refuses to open early in those cases has degraded from "quick"
 * to "locked out" for a reason its owner cannot see. Degrading to today's
 * single-anchor behaviour is the correct failure.
 *
 * Only a positive OUTSIDE verdict, or a failed triangle test, withholds it --
 * the two cases where there is real evidence that opening early is wrong.
 *
 * This gates prediction ONLY. The threshold unlock, which means someone is
 * actually standing at the door, is never gated on geometry: a mis-tuned floor
 * that refuses to open a door locks a human out of their house, and that
 * asymmetry is the same one docs/range-integrity.md already argues for the
 * per-link STS gate.
 */
bool woz_fusion_may_predict(const struct woz_fusion_verdict *v);

#endif /* WOZ_FUSION_H */
