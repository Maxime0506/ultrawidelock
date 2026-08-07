/**
 * @file woz_satellite.c — freshness gate around a second anchor's report.
 *
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */

#include "woz_satellite.h"

void woz_satellite_init(struct woz_satellite *s, const struct woz_fusion_cfg *cfg,
			uint32_t stale_ms, bool self_is_inside)
{
	if (s == NULL) {
		return;
	}
	s->cfg.baseline_mm = 0;
	s->cfg.tol_mm = 0;
	s->cfg.deadband_mm = 0;
	if (cfg != NULL) {
		s->cfg = *cfg;
	}
	s->last_ms = 0;
	s->peer_mm = 0;
	s->stale_ms = stale_ms != 0u ? stale_ms : WOZ_SATELLITE_STALE_MS_DEFAULT;
	s->self_is_inside = self_is_inside;
	s->have = false;
}

void woz_satellite_report(struct woz_satellite *s, int32_t peer_mm, int64_t now_ms)
{
	if (s == NULL || peer_mm < 0) {
		return;
	}
	s->peer_mm = peer_mm;
	s->last_ms = now_ms;
	s->have = true;
}

static bool fresh(const struct woz_satellite *s, int64_t now_ms)
{
	int64_t age;

	if (s == NULL || !s->have) {
		return false;
	}
	/*
	 * An unconfigured baseline is absence, not evidence. Without this check
	 * it reads as evidence: woz_fusion_eval() rejects a zero baseline, every
	 * pair then fails the triangle test, and a board that was merely
	 * misconfigured silently stops predicting for ever -- with a verdict that
	 * looks exactly like a detected attack. Fail back to "no satellite".
	 */
	if (s->cfg.baseline_mm <= 0) {
		return false;
	}
	age = now_ms - s->last_ms;
	/*
	 * A report from the future is a clock that moved, not a fast satellite.
	 * Treat it as unusable rather than eternally fresh: a backwards jump on
	 * this node would otherwise pin one stale distance as valid for as long
	 * as the offset lasts.
	 */
	if (age < 0) {
		return false;
	}
	return age <= (int64_t)s->stale_ms;
}

struct woz_fusion_verdict woz_satellite_verdict(const struct woz_satellite *s, int32_t self_mm,
						int64_t now_ms)
{
	struct woz_fusion_verdict none = {WOZ_SIDE_UNKNOWN, false, 0};

	if (!fresh(s, now_ms) || self_mm < 0) {
		return none;
	}
	/*
	 * woz_fusion_eval() takes (inside, outside) in that order and the sign of
	 * the difference is the whole answer, so this swap is the entire
	 * consequence of where the boards are screwed.
	 */
	if (s->self_is_inside) {
		return woz_fusion_eval(&s->cfg, self_mm, s->peer_mm);
	}
	return woz_fusion_eval(&s->cfg, s->peer_mm, self_mm);
}

bool woz_satellite_may_predict(const struct woz_satellite *s, int32_t self_mm, int64_t now_ms)
{
	struct woz_fusion_verdict v;

	/* No satellite at all, or nothing fresh: today's behaviour, unchanged. */
	if (!fresh(s, now_ms) || self_mm < 0) {
		return true;
	}
	v = woz_satellite_verdict(s, self_mm, now_ms);
	return woz_fusion_may_predict(&v);
}
