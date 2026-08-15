/* SPDX-License-Identifier: ISC */

/**
 * @file ml_feed.h — the channel-classifier glue between the CIA latch and the
 * approach controller.
 *
 * Main-loop only. When CONFIG_ULTRAWIDELOCK_ML_LOS + CONFIG_ULTRAWIDELOCK_UWB_CIRDIAG are on, a
 * trusted range is fed together with this reception's LOS/NLOS class; in every
 * other configuration both calls collapse to the plain feed and a no-op.
 */
#ifndef ML_FEED_H
#define ML_FEED_H

#include <stdint.h>

#include "ultrawidelock_approach.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Feed one trusted range, carrying this reception's channel class if any. */
enum ultrawidelock_approach_action ml_feed_range(struct ultrawidelock_approach *ap, int64_t now,
					 int32_t cm);

/** Print the debounced NLOS verdict on its edge ([ALAB] ev=ml.vote). */
void ml_feed_vote_trace(struct ultrawidelock_approach *ap, int64_t now);

#ifdef __cplusplus
}
#endif

#endif /* ML_FEED_H */
