/**
 * @file woz_satellite.h — the freshness gate around a second anchor's report.
 *
 * woz_fusion.h answers "which side of the door is this phone on" given two
 * distances measured at the same moment. This file is what makes that question
 * safe to ask on a real door, where the second distance arrives over a link
 * that can be slow, lossy or absent.
 *
 * Three rules, and they are all about what happens when the satellite is NOT
 * there:
 *
 * 1. A report older than `stale_ms` is not a report. Geometry from a stale
 *    distance is worse than no geometry, because it looks authoritative.
 * 2. No fresh report means UNKNOWN, and UNKNOWN PERMITS prediction. A satellite
 *    that has gone quiet must degrade to exactly today's behaviour, never to a
 *    door that will not open. The tree already argues this for the range gate:
 *    "A mis-tuned floor that refuses to open a door locks a human out of their
 *    house" (docs/range-integrity.md:50-53).
 * 3. Only a POSITIVE outside verdict, or a failed triangle test, withholds. Both
 *    are real evidence; absence is not. An unconfigured baseline counts as
 *    absence rather than as a failed test, so a misconfigured board degrades to
 *    today's behaviour instead of silently never predicting again.
 *
 * WHICH ANCHOR IS WHICH is a mounting fact, not a code fact, so it is a config
 * field rather than an assumption. Getting it backwards inverts the verdict --
 * it would predict for people outside and withhold from people inside, which is
 * the exact opposite of the point. `self_is_inside` makes that a decision
 * someone had to write down.
 */
#ifndef WOZ_SATELLITE_H
#define WOZ_SATELLITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "woz_fusion.h"

/** How long a satellite report stays usable. See the header comment. */
#define WOZ_SATELLITE_STALE_MS_DEFAULT 1500u

/**
 * Latest report from the second anchor, plus everything needed to judge it.
 * Caller-owned; this module allocates nothing and starts no threads.
 */
struct woz_satellite {
	struct woz_fusion_cfg cfg; /**< baseline, tolerance, dead band */
	int64_t last_ms;           /**< when the stored report arrived */
	int32_t peer_mm;           /**< satellite's distance to the phone */
	uint32_t stale_ms;         /**< older than this and peer_mm is ignored */
	bool self_is_inside;       /**< true if THIS node is the inside anchor */
	bool have;                 /**< false until the first report */
};

/**
 * @param s              Caller-owned state.
 * @param cfg            Geometry config; copied, not retained by pointer.
 * @param stale_ms       0 selects WOZ_SATELLITE_STALE_MS_DEFAULT.
 * @param self_is_inside True if the node running this code is mounted on the
 *                       inside of the door. Read the header before choosing.
 */
void woz_satellite_init(struct woz_satellite *s, const struct woz_fusion_cfg *cfg,
			uint32_t stale_ms, bool self_is_inside);

/**
 * Store a report from the second anchor.
 *
 * @param peer_mm Satellite's measured distance to the phone, millimetres.
 *                Negative is rejected outright rather than stored and rejected
 *                later, so a decode bug cannot masquerade as a stale link.
 * @param now_ms  Monotonic milliseconds on THIS node's clock, not the
 *                satellite's. Stage C owes a timebase alignment; until it
 *                exists, arrival time is the honest approximation and the
 *                staleness window is what absorbs the error.
 */
void woz_satellite_report(struct woz_satellite *s, int32_t peer_mm, int64_t now_ms);

/**
 * Evaluate the side of the door, given this node's own distance to the phone.
 *
 * @param self_mm This node's distance to the phone, millimetres.
 * @return A verdict with `geometry_ok == false` and side UNKNOWN when there is
 *         no fresh report; otherwise woz_fusion_eval()'s answer with the two
 *         distances placed according to `self_is_inside`.
 */
struct woz_fusion_verdict woz_satellite_verdict(const struct woz_satellite *s, int32_t self_mm,
						int64_t now_ms);

/**
 * The one question the approach controller asks: may prediction proceed?
 *
 * @return false ONLY on a fresh report that puts the phone outside, or on a
 *         fresh pair that no single phone position could produce. True when
 *         there is no satellite, no fresh report, or nothing to object to --
 *         see rule 2 in the header.
 */
bool woz_satellite_may_predict(const struct woz_satellite *s, int32_t self_mm, int64_t now_ms);

#endif /* WOZ_SATELLITE_H */
