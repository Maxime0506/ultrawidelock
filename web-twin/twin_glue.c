/** @file twin_glue.c — WASM entry points: the twin page's firmware harness.
 *
 * Compiled (emcc) with the untouched modules/woz_uwb sources plus the same
 * tests/host shim the host suite links, so the page runs the real responder:
 * every block is a genuinely CCM*-encrypted Pre-POLL/POLL/Response/Final/
 * Final_Data exchange decoded by the firmware's own RX state machine, and the
 * page reads its decisions through the same facade seam the lock uses.
 *
 * The peer (iPhone) side comes from tests/host/twin_frames.c — shared with
 * test_twin.c, so the page and the suite drive the responder identically. The
 * JS above supplies only the world: target distance, noise, spoof timing, and
 * the pacing of the five per-block legs (PREPOLL/POLL/TXDONE/FINAL/FINAL_DATA)
 * so a visitor can single-step a live DS-TWR round.
 *
 * Distance is injected the way physics does it: the initiator-side DS-TWR
 * intervals ride in the Final_Data as round1 = reply1 + 2*tof and
 * reply2 = round2 - 2*tof, which makes the firmware's own
 * (round1*round2 - reply1*reply2)/sum recover exactly tof ticks
 * (1 tick ~ 15.65 ps, ~4.692 mm — ccc_shim_rx.c final_data_decode).
 * A Ghost-Peak spoof is a negative-tof block through the same full path.
 */
#include <stdint.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include <deca_device_api.h>

#include "aliro_kdf.h" /* ALIRO_URSK_LEN */
#include "ccc_shim.h"
#include "fira_session.h"
#include "twin_frames.h"
#include "woz_uwb_facade.h"

/* Same session constants as test_twin.c, so the two scenarios are comparable. */
#define TWIN_SID    0x51a7c0deu
#define TWIN_STS0   0x00400000u
#define TWIN_IDX1   5000u /* first Pre-POLL's Poll_STS_Index */
#define TWIN_STRIDE 96u   /* per-block index stride the decode learns */

/* Fixed responder-side DS-TWR intervals (DTU), as test_twin.c injects them. */
#define TWIN_REPLY1 100000u
#define TWIN_ROUND2 200000u

/* No-range sentinel for the int-returning JS getters. */
#define TWIN_NO_RANGE (-100000)

static uint8_t g_ursk[ALIRO_URSK_LEN];
static uint8_t g_rcfg[17];
static struct twin_peer g_peer;
static uint32_t g_fc;      /* peer frame counter */
static uint32_t g_idx;     /* this block's POLL STS index (the reader's warm) */
static uint32_t g_blockn;  /* ranging block number on the wire */
static int g_leg;          /* next leg within the block, 0..4 */
static uint64_t g_ts;      /* rolling radio timestamp base, DTU */
static uint64_t g_t2;      /* this round's POLL RX timestamp */
static uint32_t g_latches; /* accepted latches since the page last asked */

static void twin_on_latch(void)
{
	g_latches++;
}

/** Boot the responder once per module instance (the page's Reset re-instantiates
 * the module, i.e. reboots the firmware). Returns 0 on success. */
EMSCRIPTEN_KEEPALIVE int twin_boot(void)
{
	struct woz_uwb_aliro_cfg c;
	uint8_t frame[128];
	uint16_t len;

	for (size_t i = 0; i < sizeof(g_ursk); i++) {
		g_ursk[i] = (uint8_t)(0x5Au + i);
	}
	for (size_t i = 0; i < sizeof(g_rcfg); i++) {
		g_rcfg[i] = (uint8_t)(0x20u + i);
	}
	memset(&c, 0, sizeof(c));
	c.session_id = TWIN_SID;
	c.channel = 9u;
	c.sync_code_index = 9u;
	c.slot_per_round = 12u;
	c.sts_index0 = TWIN_STS0;
	c.ursk = g_ursk;
	c.ranging_config = g_rcfg;
	c.rc_len = sizeof(g_rcfg);
	woz_host_rx_reset();
	if (woz_uwb_start_aliro(&c) != 0) {
		return -1;
	}
	twin_peer_init(&g_peer, g_ursk, TWIN_SID, TWIN_STS0);
	/* Same unlock seam the ESP32 walk-up loop blocks on (app_main.cpp). */
	woz_uwb_set_range_listener(twin_on_latch);

	/* Bootstrap: two Pre-POLL decodes learn the index + stride and seed the
	 * warm STS, exactly as the first two on-air blocks do. */
	g_fc = 200u;
	g_ts = 0x1000000ull;
	len = twin_mk_prepoll(&g_peer, frame, g_fc++, TWIN_IDX1, 0u);
	twin_stash_frame(frame, len, g_ts);
	ccc_shim_rx_try_prepoll(len);
	g_ts += 0x1000000ull;
	len = twin_mk_prepoll(&g_peer, frame, g_fc++, TWIN_IDX1 + TWIN_STRIDE, 0u);
	twin_stash_frame(frame, len, g_ts);
	ccc_shim_rx_try_prepoll(len);

	g_idx = TWIN_IDX1 + 2u * TWIN_STRIDE; /* == the warm index the decode primed */
	g_blockn = 1u;
	g_leg = 0;
	g_latches = 0u;
	return 0;
}

/** Target distance -> ToF ticks, the inverse of final_data_decode's
 * d_mm = tof * 4692 / 1000 (rounded so the round-trip is stable). */
static int32_t tof_for_cm(int32_t cm)
{
	int64_t mm10 = (int64_t)cm * 10000; /* cm -> (mm * 1000) */

	return (int32_t)((cm >= 0) ? (mm10 + 2346) / 4692 : (mm10 - 2346) / 4692);
}

/**
 * Run the next leg of the current ranging block against the live firmware:
 *   0 Pre-POLL RX   — encrypted SP0 frame; RX event arms the SP3 POLL window
 *   1 POLL result   — STS-only RFRAME at the armed index; fires Response_0 TX
 *   2 Response TXFRS — t3 captured; the Final RX window is armed
 *   3 Final RFRAME  — t6 + STS quality captured; radio reverts to SP0
 *   4 Final_Data    — dUDSK-encrypted timestamps; DS-TWR computes and latches
 * @p cm is consumed at leg 4 (the initiator timestamps carry the distance).
 * Returns the leg just executed.
 */
EMSCRIPTEN_KEEPALIVE int twin_step(int cm)
{
	uint8_t frame[128];
	uint16_t len;
	int leg = g_leg;

	switch (leg) {
	case 0: /* Pre-POLL: stash (decode deferred to the idle) + arm event */
		g_ts += 0x1000000ull;
		len = twin_mk_prepoll(&g_peer, frame, g_fc++, g_idx, g_blockn);
		twin_stash_frame(frame, len, g_ts);
		ccc_shim_rx_try_prepoll(len);
		twin_rx_event(woz_host_rx.cbs.cbRxOk, TWIN_ST_GOOD);
		break;
	case 1: /* POLL result (cper=0): t2 in, Response_0 delayed TX out */
		g_t2 = g_ts + 0x10000ull;
		woz_host_rx.rx_ts40 = g_t2;
		twin_rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_CIADONE_BIT_MASK);
		break;
	case 2: /* Response TXFRS: t3 = t2 + reply1; Final window armed */
		woz_host_rx.tx_ts40 = g_t2 + TWIN_REPLY1;
		twin_rx_event(woz_host_rx.cbs.cbTxDone, DWT_INT_TXFRS_BIT_MASK);
		break;
	case 3: /* Final RFRAME: t6 = t3 + round2, good STS; revert to SP0 */
		woz_host_rx.rx_ts40 = g_t2 + TWIN_REPLY1 + TWIN_ROUND2;
		woz_host_rx.stsq_ret = 0;
		woz_host_rx.stsq_val = 100;
		twin_rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_CIADONE_BIT_MASK);
		break;
	case 4: /* Final_Data: the initiator intervals carry the distance */
	default: {
		int32_t d = 2 * tof_for_cm(cm);

		len = twin_mk_final_data(&g_peer, frame, g_fc++, g_idx, g_blockn,
					 (uint32_t)((int64_t)TWIN_REPLY1 + d),
					 (uint32_t)((int64_t)TWIN_ROUND2 - d));
		twin_stash_frame(frame, len, g_ts + 0x30000ull);
		ccc_shim_rx_try_prepoll(len);
		g_idx += TWIN_STRIDE;
		g_blockn++;
		break;
	}
	}
	g_leg = (leg + 1) % 5;
	return leg;
}

/** Run legs to the end of the current block (one full DS-TWR exchange). */
EMSCRIPTEN_KEEPALIVE void twin_block(int cm)
{
	do {
		twin_step(cm);
	} while (g_leg != 0);
}

/* ── Read-only state for the page (all through real firmware seams) ───────── */

/** Next leg within the block, 0..4 (0 = a fresh block is about to start). */
EMSCRIPTEN_KEEPALIVE int twin_leg(void)
{
	return g_leg;
}

/** Facade telemetry seam: latest latched range, or TWIN_NO_RANGE. */
EMSCRIPTEN_KEEPALIVE int twin_last_cm(void)
{
	int32_t cm;

	return woz_uwb_last_range_cm(&cm) ? (int)cm : TWIN_NO_RANGE;
}

/** Facade unlock seam: the trusted range, or TWIN_NO_RANGE while trust is out. */
EMSCRIPTEN_KEEPALIVE int twin_trusted_cm(void)
{
	int32_t cm;

	return woz_uwb_trusted_range_cm(&cm) ? (int)cm : TWIN_NO_RANGE;
}

/** Layer-4 trust run length (0..K). */
EMSCRIPTEN_KEEPALIVE int twin_trust_level(void)
{
	return (int)fira_session_trust_level();
}

/** FIRA_RANGE_TRUST_K, straight from the compiled firmware. */
EMSCRIPTEN_KEEPALIVE int twin_trust_k(void)
{
	return FIRA_RANGE_TRUST_K;
}

/** Layer-1 plausibility predicate (for the self-test). */
EMSCRIPTEN_KEEPALIVE int twin_plausible(int cm)
{
	return fira_session_range_plausible(cm) ? 1 : 0;
}

/** Accepted-latch count since last asked (the app_main task-notify analogue);
 * reading clears it. A rejected block never wakes this seam. */
EMSCRIPTEN_KEEPALIVE int twin_take_latches(void)
{
	int n = (int)g_latches;

	g_latches = 0u;
	return n;
}

/** True while the SP3 RX is armed for the POLL (ccc_shim_rx state). */
EMSCRIPTEN_KEEPALIVE int twin_awaiting_poll(void)
{
	return ccc_shim_rx_awaiting_poll() ? 1 : 0;
}

/** Radio-stub call counters + wire-side identities, for the debug panel. */
EMSCRIPTEN_KEEPALIVE unsigned twin_stat_rxenable(void)
{
	return woz_host_rx.rxenable_calls;
}

EMSCRIPTEN_KEEPALIVE unsigned twin_stat_starttx(void)
{
	return woz_host_rx.starttx_calls;
}

EMSCRIPTEN_KEEPALIVE unsigned twin_poll_index(void)
{
	return g_idx;
}

EMSCRIPTEN_KEEPALIVE unsigned twin_block_no(void)
{
	return g_blockn;
}
