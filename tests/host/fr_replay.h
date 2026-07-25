/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * fr_replay — host-side replay of a flight-recorder trace.
 *
 * Feeds a recorded walk-up's inputs (session config + the ordered per-frame
 * register snapshots) back through the REAL ccc_shim_rx.c listener via the
 * DW3000 shim doubles (woz_host_rx), and captures the radio-action outputs the
 * engine produces at each event. Because the responder path has no RNG and
 * every clock read is a captured timestamp, a correct firmware re-derives the
 * exact same outputs — so a divergence pins a behaviour change to one event.
 *
 * Lives in tests/host (not the portable module) because it pokes the shim's
 * woz_host_rx state, which only exists in the host build.
 */
#ifndef WOZ_FR_REPLAY_H
#define WOZ_FR_REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flight_recorder.h"

#define FR_REPLAY_MAX_EV 512u

/* The observable radio-action state after one replayed event — the "outputs"
 * a firmware change would perturb. */
struct fr_output {
	uint8_t ep;                 /* the entry point this event drove */
	unsigned rxenable_calls;    /* cumulative __real_dwt_rxenable count */
	int32_t last_rxenable_mode; /* mode of the last RX arm */
	unsigned starttx_calls;     /* cumulative Response TX count */
	unsigned forcetrxoff_calls; /* cumulative radio-off count */
};

struct fr_replay_result {
	bool ok;              /* trace parsed and replayed to a clean END */
	int err_at;          /* record index of a parse/format error, else -1 */
	uint16_t port;        /* META port that produced the trace */
	char sha[FR_SHA_MAX + 1];
	unsigned n_events;    /* events replayed (== outputs filled) */
	unsigned end_n_events;/* n_events the END record claimed */
	bool truncated;       /* END truncation flag */
	struct fr_output out[FR_REPLAY_MAX_EV];
	int32_t range_cm;     /* latched DS-TWR range after the last event */
	bool range_valid;     /* a range was latched */
};

/* Replay `trace[0..len)`. Resets the shim, reconstructs the session from the
 * CONFIG record, drives every EV record through the listener, and fills *out.
 * Returns true iff the trace was well-formed through its END record. */
bool fr_replay_run(const uint8_t *trace, size_t len, struct fr_replay_result *out);

#endif /* WOZ_FR_REPLAY_H */
