/**
 * @file side_feed.h — lab ingest of BLE witness summaries into ultrawidelock_side.
 *
 * Line format (ASCII, one window):
 *   SF1 in=<dbm> out=<dbm> th=<dbm> ni=<n> no=<n> nt=<n>
 *
 * Absent anchors use ni/no/nt = 0 (RSSI ignored). Threshold may be nt=0.
 * Fed over SEGGER RTT down-buffer when CONFIG_ULTRAWIDELOCK_SIDE_FEED_RTT=y.
 */
#ifndef SIDE_FEED_H
#define SIDE_FEED_H

#include <stdbool.h>

#include "ultrawidelock_side.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parse one SF1 line into features (now_ms/seq filled by caller). */
bool side_feed_parse_sf1(const char *line, struct ultrawidelock_side_features *out);

/** Push a parsed feature set into the inbox (overwrites prior unread). */
void side_feed_push(const struct ultrawidelock_side_features *feat);

/** Pop newest features if any; returns true once per push. */
bool side_feed_take(struct ultrawidelock_side_features *out);

/** Drain RTT down-buffer for SF1 lines (no-op if FEED_RTT off). */
void side_feed_rtt_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* SIDE_FEED_H */
