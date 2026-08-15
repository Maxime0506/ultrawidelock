/* SPDX-License-Identifier: ISC */

/**
 * @file ultrawidelock_satellite.h — the freshness gate around a second anchor's report.
 *
 * ultrawidelock_fusion.h answers "which side of the door is this phone on" given two
 * distances measured at the same moment. This file is what makes that question
 * safe to ask on a real door, where the second distance arrives over a link
 * that can be slow, lossy or absent.
 *
 * Three rules, all about the satellite NOT being there: a report older than
 * `stale_ms` is not a report; no fresh report means UNKNOWN, and UNKNOWN
 * PERMITS prediction (a quiet satellite degrades to today's behaviour, never to
 * a door that will not open); only a POSITIVE outside verdict or a failed
 * triangle test withholds -- absence is not evidence, and an unconfigured
 * baseline counts as absence. `self_is_inside` is a config field because which
 * anchor is which is a mounting fact; backwards, it inverts the verdict.
 */
#ifndef ULTRAWIDELOCK_SATELLITE_H
#define ULTRAWIDELOCK_SATELLITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ultrawidelock_fusion.h"

/** How long a satellite report stays usable. See the header comment. */
#define ULTRAWIDELOCK_SATELLITE_STALE_MS_DEFAULT 1500u

/**
 * Latest report from the second anchor, plus everything needed to judge it.
 * Caller-owned; this module allocates nothing and starts no threads.
 */
struct ultrawidelock_satellite {
	struct ultrawidelock_fusion_cfg cfg; /**< baseline, tolerance, dead band */
	int64_t last_ms;           /**< when the stored report arrived */
	int32_t peer_mm;           /**< satellite's distance to the phone */
	uint32_t stale_ms;         /**< older than this and peer_mm is ignored */
	bool self_is_inside;       /**< true if THIS node is the inside anchor */
	bool have;                 /**< false until the first report */
};

/**
 * @param s              Caller-owned state.
 * @param cfg            Geometry config; copied, not retained by pointer.
 * @param stale_ms       0 selects ULTRAWIDELOCK_SATELLITE_STALE_MS_DEFAULT.
 * @param self_is_inside True if the node running this code is mounted on the
 *                       inside of the door. Read the header before choosing.
 */
void ultrawidelock_satellite_init(struct ultrawidelock_satellite *s,
				  const struct ultrawidelock_fusion_cfg *cfg, uint32_t stale_ms,
				  bool self_is_inside);

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
void ultrawidelock_satellite_report(struct ultrawidelock_satellite *s, int32_t peer_mm,
				    int64_t now_ms);

/**
 * Evaluate the side of the door, given this node's own distance to the phone.
 *
 * @param self_mm This node's distance to the phone, millimetres.
 * @return A verdict with `geometry_ok == false` and side UNKNOWN when there is
 *         no fresh report; otherwise ultrawidelock_fusion_eval()'s answer with the two
 *         distances placed according to `self_is_inside`.
 */
struct ultrawidelock_fusion_verdict
ultrawidelock_satellite_verdict(const struct ultrawidelock_satellite *s, int32_t self_mm,
				int64_t now_ms);

/**
 * The one question the approach controller asks: may prediction proceed?
 *
 * @return false ONLY on a fresh report that puts the phone outside, or on a
 *         fresh pair that no single phone position could produce. True when
 *         there is no satellite, no fresh report, or nothing to object to --
 *         see rule 2 in the header.
 */
bool ultrawidelock_satellite_may_predict(const struct ultrawidelock_satellite *s, int32_t self_mm,
					 int64_t now_ms);

#endif /* ULTRAWIDELOCK_SATELLITE_H */
