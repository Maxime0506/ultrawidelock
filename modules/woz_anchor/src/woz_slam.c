/**
 * @file woz_slam.c — impact and tamper classification (implementation).
 *
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */

#include "woz_slam.h"

void woz_slam_init(struct woz_slam_state *s)
{
	if (s == NULL) {
		return;
	}
	s->last_accept_ms = 0;
	s->window_start_ms = 0;
	s->window_count = 0u;
	s->tamper_latched = false;
	s->seeded = false;
}

enum woz_slam_event woz_slam_poll(const struct woz_slam_cfg *cfg, struct woz_slam_state *s,
				  bool struck, int64_t now_ms)
{
	int64_t since_accept, since_window;

	if (cfg == NULL || s == NULL || cfg->tamper_count == 0u || !struck) {
		return WOZ_SLAM_NONE;
	}

	/*
	 * The first strike after init has no previous timestamp to be debounced
	 * against. Comparing against a zeroed last_accept_ms would work at boot
	 * (uptime is small) and would silently stop working for a board that has
	 * been up longer than debounce_ms, which is every board. `seeded` makes
	 * the first strike unconditional instead of accidentally correct.
	 */
	if (s->seeded) {
		since_accept = now_ms - s->last_accept_ms;
		/*
		 * A backwards clock means the caller's timebase moved, not that
		 * the door was hit twice. Treat it as outside the debounce so
		 * the event is accepted rather than dropped: missing a real
		 * strike is the worse failure for a tamper signal.
		 */
		if (since_accept >= 0 && since_accept < (int64_t)cfg->debounce_ms) {
			return WOZ_SLAM_NONE;
		}
	}

	s->last_accept_ms = now_ms;
	s->seeded = true;

	/* Open a fresh burst window, or extend the one already running. */
	since_window = now_ms - s->window_start_ms;
	if (s->window_count == 0u || since_window < 0 ||
	    since_window > (int64_t)cfg->tamper_window_ms) {
		s->window_start_ms = now_ms;
		s->window_count = 1u;
	} else if (s->window_count < UINT8_MAX) {
		s->window_count++;
	}

	if (s->window_count >= cfg->tamper_count) {
		/*
		 * Report the transition, not the condition. Returning TAMPER on
		 * every subsequent strike would let an attacker choose how many
		 * times the door node logs, which is a log-flood primitive on a
		 * board whose console is a 4 KB RTT ring.
		 */
		if (!s->tamper_latched) {
			s->tamper_latched = true;
			return WOZ_SLAM_TAMPER;
		}
		return WOZ_SLAM_NONE;
	}

	return WOZ_SLAM_IMPACT;
}

bool woz_slam_tampered(const struct woz_slam_state *s)
{
	return s != NULL && s->tamper_latched;
}

void woz_slam_clear_tamper(struct woz_slam_state *s)
{
	if (s == NULL) {
		return;
	}
	s->tamper_latched = false;
	s->window_count = 0u;
}
