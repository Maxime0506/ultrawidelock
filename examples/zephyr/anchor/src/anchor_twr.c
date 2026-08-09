/**
 * @file anchor_twr.c — anchor-to-anchor DS-TWR, both roles (implementation).
 */

#include "anchor_twr.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <deca_device_api.h>

#include "ds_twr.h"   /* ds_twr_tof_signed(): the shared estimator */
#include "uwb_min.h"  /* uwb_min_radio_init() */
#include "uwb_seam.h" /* woz_uwb_arm_rx / woz_uwb_configure_phy */
#include "woz_log.h"
#include "woz_port.h"

LOG_MODULE_REGISTER(anchor_twr, LOG_LEVEL_INF);

/* ── wire format ──────────────────────────────────────────────────────────────
 *
 * Ours end to end, so it is the smallest thing that carries the two intervals
 * the responder cannot measure itself. No CCC MHR: nothing on the far end
 * parses one, and a 23-byte header would only be there to look official.
 */
#define ANCHOR_MAGIC0 'A'
#define MSG_POLL      'P'
#define MSG_RESP      'R'
#define MSG_FINAL     'F'

#define OFF_MAGIC  0u
#define OFF_TYPE   1u
#define OFF_SEQ    2u
#define POLL_LEN   6u  /* magic, type, seq32 */
#define RESP_LEN   6u  /* same */
#define OFF_ROUND1 6u  /* FINAL only */
#define OFF_REPLY2 10u /* FINAL only */
#define FINAL_LEN  14u
/* FCS_LEN comes from deca_device_api.h (2 bytes); defining it here shadows the
 * driver's and warns. The chip appends the FCS itself, so every length written
 * to dwt_writetxfctrl() carries it and every length read back has to lose it. */

/* ── timing ───────────────────────────────────────────────────────────────────
 *
 * The DW3000 delayed-TX register takes the high 32 bits of the 40-bit system
 * time, so one unit is 256 DTU. 1 DTU = 1/(499.2e6 * 128) s = 15.65 ps, hence
 * 1 hi32 = 4.006 ns and 249.6 hi32 per microsecond. ccc_shim_rx.c prints its
 * arm margins with `/250` on the same basis.
 */
#define HI32_PER_US 250u

/* Distance per DTU: c / (499.2e6 * 128) = 4.69176 mm. The engine's responder
 * path uses the same constant as `tof * 4692 / 1000` (ccc_shim_rx.c:631) and
 * this stays bit-identical to it on purpose -- two different numbers for the
 * same physical constant is how two ports drift apart. */
#define MM_PER_DTU_NUM 4692
#define MM_PER_DTU_DEN 1000

/* ipatovFpIndex is Q10.6: six fractional bits, so the integer part is the
 * accumulator tap index. Duplicated from uwb_cirdiag.c's CIRDIAG_FP_FRAC_BITS
 * rather than shared, to keep this bench app free of a dependency on a module
 * it otherwise does not use. */
#define FP_INDEX_FRAC_BITS 6u
#define FP_INDEX_FRAC_MASK 63u

/* Plausibility band, layer 1 of the engine's range integrity.
 * Below -30 cm is physically impossible; above 30 m is outside any proximity
 * envelope and, on this bench link, means a timestamp came from the wrong round. */
#define ANCHOR_MIN_MM (-300)
#define ANCHOR_MAX_MM (30 * 1000)

/*
 * OFF BY DEFAULT, AND KEPT ONLY AS THE RECORD OF A WRONG TURN.
 *
 * This was built to outvote what looked like a sporadic minority mode: eight
 * stationary samples at a tape-measured 1.000 m read 929, 975, 905, 929, 910,
 * 619, 872, 600 mm, two of eight low by ~310 mm. Seventy samples later the
 * false mode was 37% and persistent, and the median removed almost none of it
 * -- 149.1 mm mean absolute error raw against 129.1 mm filtered.
 *
 * The defect was the delayed-TX timestamp, not the leading-edge detector. See
 * CONFIG_ANCHOR_TX_512_ALIGN and CONFIG_ANCHOR_MEDIAN_N's help text. With the
 * alignment on the range is unimodal at 19.6 mm sd, so this has nothing to do.
 *
 * N is odd BY CONSTRUCTION: an even window must combine its two middle samples,
 * and against a bimodal population those two can be one from each mode, so
 * their average lands in the gap between the modes -- the one value the
 * measurement never takes. Hence a BUILD_ASSERT rather than a comment.
 */
#define MEDIAN_N CONFIG_ANCHOR_MEDIAN_N
BUILD_ASSERT(MEDIAN_N % 2 == 1, "ANCHOR_MEDIAN_N must be odd; an even window averages "
			       "across the two modes and lands between them");

/* MEDIAN_N * 4 B of .bss, and the same again of stack for the sorted copy. 20 B
 * and 20 B at the default of 5. */
static int32_t g_win[MEDIAN_N];
static uint8_t g_win_n;   /* valid entries, saturating at MEDIAN_N */
static uint8_t g_win_pos; /* next slot to overwrite */

/*
 * Push one raw distance, return the window's median.
 *
 * Produces a value from the first sample rather than waiting for a full window,
 * because a bring-up run should print something. With an even number of valid
 * entries it takes the UPPER of the two middles instead of averaging them:
 * averaging is banned for the reason above, and upper is the correct side to err
 * on, because every known failure of the leading-edge detector is short.
 */
static int32_t median_push(int32_t mm)
{
	int32_t sorted[MEDIAN_N];
	uint8_t i, j;

	g_win[g_win_pos] = mm;
	g_win_pos = (uint8_t)((g_win_pos + 1u) % MEDIAN_N);
	if (g_win_n < MEDIAN_N) {
		g_win_n++;
	}

	/* Insertion sort on a copy: MEDIAN_N <= 15, so the quadratic cost is a few
	 * hundred cycles and it needs no scratch beyond the copy. */
	for (i = 0u; i < g_win_n; i++) {
		int32_t v = g_win[i];

		for (j = i; j > 0u && sorted[j - 1u] > v; j--) {
			sorted[j] = sorted[j - 1u];
		}
		sorted[j] = v;
	}

	return sorted[g_win_n / 2u];
}

/* Chip-level RX window, in dwt_setrxtimeout units of ~1.0256 us. Sized for a
 * reply leg: the peer sends one reply delay from now, so this is that plus a
 * generous margin. A window too tight turns a working round into a silent
 * timeout, and this is a bench link with no airtime to conserve. */
#define RX_WINDOW_UNITS (CONFIG_ANCHOR_REPLY_DELAY_US * 2u + 3000u)

/* How long to spin on SYS_STATUS for a reply leg, milliseconds. Comfortably
 * past RX_WINDOW_UNITS so the chip's own timeout is what ends a quiet window,
 * which is the case the counters can tell apart. */
#define REPLY_CEIL_MS 50

/* And how long the responder spins waiting for the NEXT round's POLL.
 *
 * Not the same number, and the difference matters. The responder is deaf between
 * dropping out of the wait and re-arming, so a ceiling shorter than the round
 * period makes it re-arm several times per round and lose a POLL to one of those
 * gaps -- which would show up as a drop rate that no amount of antenna work
 * improves. Twice CONFIG_ANCHOR_ROUND_PERIOD_MS means the wait normally spans a
 * whole round and the gap is entered once per two rounds at worst. */
#define POLL_CEIL_MS (CONFIG_ANCHOR_ROUND_PERIOD_MS * 2)

/* The chip must not time out before that ceiling either, or the re-arm gap comes
 * back by another route. 0 disables the hardware RX timeout entirely, which is
 * what the POLL wait wants: it is the app's ceiling that bounds it. */
#define RX_WINDOW_NONE 0u

static struct anchor_twr_stats g_st;
static uint8_t g_tx[FINAL_LEN];
/* Sized to the longest frame this protocol has, with rx_expect() refusing
 * anything longer before it reads. The FCS never reaches here: the chip strips
 * nothing, but the length check subtracts it and only the body is read out. */
static uint8_t g_rx[FINAL_LEN];

#define RX_EVENT_MASK                                                                              \
	(DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFTO_BIT_MASK |                \
	 DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXSTO_BIT_MASK |                \
	 DWT_INT_RXFSL_BIT_MASK | DWT_INT_ARFE_BIT_MASK)
#define RX_TIMEOUT_MASK (DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXSTO_BIT_MASK)

/**
 * @brief The anchor PHY: channel 9, 6.8 Mbps, STS OFF.
 *
 * uwb_min.c's baseline runs DWT_STS_MODE_ND, which radiates the scrambled
 * sequence and NO payload -- correct for the CCC RFRAMEs it exists to serve,
 * and useless here, because the FINAL has to carry two 32-bit intervals. SP0
 * with STS off is the plain data frame this protocol needs.
 *
 * Nothing is lost by dropping STS on this link: STS defends a distance against
 * an attacker who wants it shortened, and both ends of an anchor-to-anchor link
 * are ours. The phone-facing link keeps its STS and is untouched by any of this.
 */
static const dwt_config_t g_anchor_cfg = {
	.chan = 9,
	.txPreambLength = DWT_PLEN_128,
	.rxPAC = DWT_PAC8,
	.txCode = 11,
	.rxCode = 11,
	.sfdType = DWT_SFD_IEEE_4Z,
	.dataRate = DWT_BR_6M8,
	.phrMode = DWT_PHRMODE_STD,
	.phrRate = DWT_PHRRATE_STD,
	.sfdTO = (129 + 8 - 8),
	.stsMode = DWT_STS_MODE_OFF,
	.stsLength = DWT_STS_LEN_64,
	.pdoaMode = DWT_PDOA_M0,
};

/** @brief Channel 9 TX power / pulse shaper, as uwb_min.c uses. */
static const dwt_txconfig_t g_anchor_txcfg = {
	.PGdly = 0x34,
	.power = 0xfdfdfdfdUL,
	.PGcount = 0,
};

/** @brief Store a uint32 little-endian. */
static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/** @brief Load a uint32 little-endian. */
static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/** @brief Assemble a 5-byte DW3000 (40-bit) timestamp into a uint64 of DTU ticks. */
static uint64_t ts5_to_u64(const uint8_t t[5])
{
	return (uint64_t)t[0] | ((uint64_t)t[1] << 8) | ((uint64_t)t[2] << 16) |
	       ((uint64_t)t[3] << 24) | ((uint64_t)t[4] << 32);
}

/** @brief Read the last RX RMARKER as DTU. STS is off here, so Ipatov is the only path. */
static uint64_t rx_ts(void)
{
	uint8_t t[5];

	dwt_readrxtimestamp_ipatov(t);
	return ts5_to_u64(t);
}

/** @brief Read the last TX RMARKER as DTU. */
static uint64_t tx_ts(void)
{
	uint8_t t[5];

	dwt_readtxtimestamp(t);
	return ts5_to_u64(t);
}

/**
 * @brief Spin on SYS_STATUS until one of @p mask is set, or @p ceil_ms expires.
 *
 * A busy poll, on purpose. Sleeping between reads would be kinder to the SPI bus
 * and would also add its whole period to the gap between a POLL landing and the
 * reply being scheduled -- and that gap is the one thing this protocol cannot
 * spend, because the reply has to be programmed before its slot arrives. There
 * is nothing else for this app to do with the CPU.
 *
 * @return the status word; the caller checks which bit it got.
 */
static uint32_t wait_status(uint32_t mask, int ceil_ms)
{
	const int64_t deadline = woz_uptime_ms() + ceil_ms;
	uint32_t st = 0;

	while (((st = dwt_readsysstatuslo()) & mask) == 0u) {
		if (woz_uptime_ms() > deadline) {
			break;
		}
	}
	return st;
}

/**
 * @brief Receive one frame of the expected type into g_rx.
 *
 * @param type Expected message type byte.
 * @param ts_out RX RMARKER in DTU, written on success.
 * @param ceil_ms How long to spin before giving up. The two callers want very
 *        different values: a reply leg knows the frame is one reply delay away,
 *        while the responder waiting for the next POLL is waiting a whole round
 *        period and must not keep dropping out of RX to be asked again.
 * @return frame length on success, -ETIMEDOUT on a quiet window, -EBADMSG otherwise.
 */
static int rx_expect(uint8_t type, uint64_t *ts_out, int ceil_ms)
{
	uint32_t st;
	uint16_t len;
	uint8_t rng = 0;

	st = wait_status(RX_EVENT_MASK, ceil_ms);
	if ((st & DWT_INT_RXFCG_BIT_MASK) == 0u) {
		dwt_writesysstatuslo(st & RX_EVENT_MASK);
		dwt_forcetrxoff();
		return (st & RX_TIMEOUT_MASK) ? -ETIMEDOUT : -EBADMSG;
	}

	/* The reported length includes the FCS the chip appended. Anything shorter
	 * than a POLL or longer than a FINAL is not one of ours: channel 9 carries
	 * other traffic, including this project's own CCC rounds, and a foreign
	 * frame is a normal event on a bench rather than an error worth logging. */
	len = (uint16_t)(dwt_getframelength(&rng) & 0x3FFu);
	if (len < POLL_LEN + FCS_LEN || len > FINAL_LEN + FCS_LEN) {
		dwt_writesysstatuslo(st & RX_EVENT_MASK);
		return -EBADMSG;
	}
	len = (uint16_t)(len - FCS_LEN);
	dwt_readrxdata(g_rx, len, 0);
	*ts_out = rx_ts();
	dwt_writesysstatuslo(st & RX_EVENT_MASK);

	if (g_rx[OFF_MAGIC] != ANCHOR_MAGIC0 || g_rx[OFF_TYPE] != type) {
		return -EBADMSG;
	}
	return (int)len;
}

/**
 * @brief Transmit @p len bytes immediately and return the TX RMARKER.
 * @return 0 on success with @p ts_out written, -EIO if the frame never left.
 */
static int tx_now(uint16_t len, uint64_t *ts_out)
{
	uint32_t st;

	if (dwt_writetxdata(len, g_tx, 0) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl((uint16_t)(len + FCS_LEN), 0, 1);
	if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
		return -EIO;
	}
	st = wait_status(DWT_INT_TXFRS_BIT_MASK, REPLY_CEIL_MS);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	if ((st & DWT_INT_TXFRS_BIT_MASK) == 0u) {
		return -EIO;
	}
	*ts_out = tx_ts();
	return 0;
}

/**
 * @brief The delayed-TX register value one reply delay after @p ref_dtu.
 *
 * Single definition on purpose: the initiator needs this value BEFORE it writes
 * the FINAL payload (t5 goes inside the frame) and again when it arms the
 * transmit, and two copies of the arithmetic that must agree exactly is how the
 * two drift apart under a later edit.
 */
static uint32_t reply_hi32(uint64_t ref_dtu)
{
	uint32_t dx = (uint32_t)((ref_dtu >> 8) + CONFIG_ANCHOR_REPLY_DELAY_US * HI32_PER_US);

#if IS_ENABLED(CONFIG_ANCHOR_TX_512_ALIGN)
	/* Even, so (dx << 8) is a multiple of 512 DTU and the prediction in
	 * tx_delayed() is exact rather than 256 DTU early half the time. See
	 * CONFIG_ANCHOR_TX_512_ALIGN for the measurement that made this necessary.
	 *
	 * BOTH USES OF THIS FUNCTION GET THE MASKED VALUE, which is the point of it
	 * living here rather than at either call site: the initiator computes the
	 * FINAL's payload from it and then schedules the transmit from it, and the
	 * two agreeing is what makes the payload's reply2 describe the transmit
	 * that actually happened. */
	dx &= ~1u;
#endif
	return dx;
}

/**
 * @brief Schedule a delayed transmit one reply delay after @p ref_dtu.
 *
 * The RMARKER the chip will produce is (dx << 8) plus whatever the TX antenna
 * delay register holds, so it is knowable BEFORE the frame goes out -- which is
 * what lets the initiator put t5 inside the FINAL it is about to send. Nothing
 * in this repo ever programs that register (modules/woz_dw3000's only writers are in
 * the MCPS init path this stack does not use), so the offset is a constant, and
 * a constant is exactly what ANCHOR_ANT_DLY_DTU absorbs. The residual is
 * measured rather than assumed: every delayed TX compares the prediction with
 * the RMARKER the chip actually reports and keeps the worst gap in
 * anchor_twr_stats::t5_err_max.
 *
 * @param ref_dtu Reference RMARKER (the frame that triggered this reply).
 * @param len Frame length to send, bytes, excluding FCS.
 * @param pred_out Predicted TX RMARKER in DTU.
 * @return 0 if the chip accepted the schedule, -ETIME if the slot was already past.
 */
static int tx_delayed(uint64_t ref_dtu, uint16_t len, uint64_t *pred_out)
{
	uint32_t dx = reply_hi32(ref_dtu);

	dwt_setdelayedtrxtime(dx);
	if (dwt_writetxdata(len, g_tx, 0) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl((uint16_t)(len + FCS_LEN), 0, 1);
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		/* HPDWARN: the programmed instant had already gone by when the
		 * chip looked. On this part that means the arm path took longer
		 * than ANCHOR_REPLY_DELAY_US, which is the one number to raise. */
		g_st.late++;
		dwt_forcetrxoff();
		return -ETIME;
	}
	*pred_out = (uint64_t)dx << 8;
	return 0;
}

/**
 * @brief After a delayed TX completes, fold the prediction error into the stats.
 *
 * Magnitude, not a signed difference: the antenna delay makes the actual RMARKER
 * LATER than the schedule, so the expected sign is known and only the size is
 * news. Taken as an absolute value rather than a wrapped subtraction, because a
 * wrap would turn "1 tick early" into 4.29 billion and hide the interesting case
 * behind an obviously-broken number.
 */
static void note_tx_prediction(uint64_t predicted)
{
	uint64_t actual = tx_ts();
	uint64_t err = (actual >= predicted) ? (actual - predicted) : (predicted - actual);

	if (err > UINT32_MAX) {
		err = UINT32_MAX;
	}
	if ((uint32_t)err > g_st.t5_err_max) {
		g_st.t5_err_max = (uint32_t)err;
	}
	if ((uint32_t)err < g_st.t5_err_min) {
		g_st.t5_err_min = (uint32_t)err;
	}
}

/*
 * Start the high-frequency crystal and wait for it.
 *
 * WHY THIS APP NEEDS IT AND THE LOCK DOES NOT. The lock links MPSL for the
 * radio, and MPSL keeps the HFXO running for its own reasons -- the built
 * .config differs by exactly CONFIG_CLOCK_CONTROL_MPSL=y and
 * CONFIG_CLOCK_CONTROL_NRF_FORCE_ALT=y. This app has CONFIG_BT=n and no other
 * radio, so nothing asks for the crystal and the SoC stays on the internal RC
 * oscillator. Every DW3000 bring-up in this tree has therefore had the HFXO
 * running by accident rather than by request.
 *
 * Blocking start, not the async one: this runs once at init with nothing else
 * to do, and a clock that is merely "starting" when the first SPI transfer goes
 * out is the situation being fixed.
 */
static void anchor_hfclk_start(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF)
	const struct device *clk = DEVICE_DT_GET_ONE(nordic_nrf_clock);

	if (!device_is_ready(clk)) {
		LOG_WRN("clock controller not ready; HFXO not requested");
		return;
	}
	if (clock_control_on(clk, CLOCK_CONTROL_NRF_SUBSYS_HF) != 0) {
		LOG_WRN("HFXO start failed; running on the internal RC");
		return;
	}
	LOG_INF("HFXO running");
#endif
}

int anchor_twr_init(void)
{
	int rc;

	anchor_hfclk_start();

	rc = uwb_min_radio_init();
	if (rc != 0) {
		LOG_ERR("radio init failed: %d", rc);
		return rc;
	}

	dwt_forcetrxoff();
	if (woz_uwb_configure_phy((dwt_config_t *)&g_anchor_cfg) != DWT_SUCCESS) {
		LOG_ERR("anchor PHY configure failed");
		return -EIO;
	}
	dwt_configuretxrf((dwt_txconfig_t *)&g_anchor_txcfg);
	dwt_setrxtimeout(RX_WINDOW_UNITS);

	/* WITHOUT THIS ipatovFpIndex IS NOT POPULATED, and reads as a stale or zero
	 * value that looks like a measurement. The CIA only logs its diagnostics
	 * when asked to. LOG_ALL and not LOG_ALL|LOG_MAX: the MAX bit adds a
	 * double-buffer copy that only the peak-amplitude fields need, and nothing
	 * here reads those. Same choice modules/woz_uwb/src/driver/uwb_cirdiag.c
	 * makes in its lean mode. */
	dwt_configciadiag((uint8_t)DW_CIA_DIAG_LOG_ALL);

	/* Not zero: this is a running minimum, and g_st is zero-initialised, so
	 * leaving it would pin the minimum at 0 forever and report a spread that
	 * does not exist. */
	g_st.t5_err_min = UINT32_MAX;

	LOG_INF("anchor PHY: ch9 6M8 STS off, reply delay %u us, ant dly %d DTU",
		(unsigned int)CONFIG_ANCHOR_REPLY_DELAY_US, (int)CONFIG_ANCHOR_ANT_DLY_DTU);
	return 0;
}

int anchor_twr_initiator_round(uint32_t seq)
{
	uint64_t t1, t4, t5_pred;
	int rc;

	g_st.rounds++;
	dwt_forcetrxoff();

	g_tx[OFF_MAGIC] = ANCHOR_MAGIC0;
	g_tx[OFF_TYPE] = MSG_POLL;
	put_le32(&g_tx[OFF_SEQ], seq);
	rc = tx_now(POLL_LEN, &t1);
	if (rc != 0) {
		return rc;
	}

	if (woz_uwb_arm_rx(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
		return -EIO;
	}
	rc = rx_expect(MSG_RESP, &t4, REPLY_CEIL_MS);
	if (rc < 0) {
		if (rc == -ETIMEDOUT) {
			g_st.timeouts++;
		}
		return rc;
	}
	if (get_le32(&g_rx[OFF_SEQ]) != seq) {
		return -EBADMSG; /* a straggler from an earlier round */
	}

	/* t_round1 and t_reply2 are what the responder cannot measure. Both are
	 * 32-bit differences of a 40-bit counter, which is what struct ds_twr takes
	 * and why the wrap arithmetic below is deliberate rather than sloppy. */
	g_tx[OFF_TYPE] = MSG_FINAL;
	put_le32(&g_tx[OFF_ROUND1], (uint32_t)(t4 - t1));

	/* The FINAL carries its own TX time, so the schedule has to be known
	 * before the payload is written -- hence reply_hi32() here and again
	 * inside tx_delayed(), rather than a second expression that has to match. */
	t5_pred = (uint64_t)reply_hi32(t4) << 8;
	put_le32(&g_tx[OFF_REPLY2], (uint32_t)(t5_pred - t4));

	rc = tx_delayed(t4, FINAL_LEN, &t5_pred);
	if (rc != 0) {
		return rc;
	}
	if ((wait_status(DWT_INT_TXFRS_BIT_MASK, REPLY_CEIL_MS) & DWT_INT_TXFRS_BIT_MASK) == 0u) {
		return -EIO;
	}
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	note_tx_prediction(t5_pred);
	return 0;
}

int anchor_twr_responder_round(int32_t *mm_out, uint32_t *seq_out)
{
	struct ds_twr tw;
	uint64_t t2, t3_pred, t6;
	uint32_t seq;
	int32_t tof;
	int32_t mm;
	int rc;

	dwt_forcetrxoff();
	/* No hardware timeout for the POLL wait: the app's ceiling bounds it, and
	 * a chip timeout here would drop the receiver mid-round. Restored to the
	 * reply-leg window below, before the first leg that needs one. */
	dwt_setrxtimeout(RX_WINDOW_NONE);
	if (woz_uwb_arm_rx(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
		return -EIO;
	}
	rc = rx_expect(MSG_POLL, &t2, POLL_CEIL_MS);
	if (rc < 0) {
		if (rc == -ETIMEDOUT) {
			g_st.timeouts++;
		}
		return rc;
	}
	seq = get_le32(&g_rx[OFF_SEQ]);
	g_st.rounds++;
	/* Back to a bounded window: the FINAL is one reply delay away and a wait
	 * with no timeout would leave the receiver on until the app ceiling if the
	 * initiator went quiet mid-round. */
	dwt_setrxtimeout(RX_WINDOW_UNITS);

	g_tx[OFF_MAGIC] = ANCHOR_MAGIC0;
	g_tx[OFF_TYPE] = MSG_RESP;
	put_le32(&g_tx[OFF_SEQ], seq);
	rc = tx_delayed(t2, RESP_LEN, &t3_pred);
	if (rc != 0) {
		return rc;
	}
	if ((wait_status(DWT_INT_TXFRS_BIT_MASK, REPLY_CEIL_MS) & DWT_INT_TXFRS_BIT_MASK) == 0u) {
		return -EIO;
	}
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	/* The responder is not on the clock the way the initiator is: it can read
	 * the RMARKER the chip actually produced instead of trusting the schedule,
	 * so it does. The prediction is still checked, because it is the initiator's
	 * assumption and this is the only place that can measure it. */
	note_tx_prediction(t3_pred);
	{
		uint64_t t3 = tx_ts();

		if (woz_uwb_arm_rx(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
			return -EIO;
		}
		rc = rx_expect(MSG_FINAL, &t6, REPLY_CEIL_MS);
		if (rc < 0) {
			if (rc == -ETIMEDOUT) {
				g_st.timeouts++;
			}
			return rc;
		}
		if (rc < (int)FINAL_LEN || get_le32(&g_rx[OFF_SEQ]) != seq) {
			return -EBADMSG;
		}

		tw.t_round1 = get_le32(&g_rx[OFF_ROUND1]);
		tw.t_reply2 = get_le32(&g_rx[OFF_REPLY2]);
		tw.t_reply1 = (uint32_t)(t3 - t2);
		tw.t_round2 = (uint32_t)(t6 - t3);
	}

	/* The shared estimator: src/fira/ds_twr.c, the same one the Aliro responder
	 * uses. Signed, which matters here more than anywhere -- 0.3 m is the first
	 * row of stage A's measurement matrix and that is exactly where an unsigned
	 * numerator underflows. */
	tof = ds_twr_tof_signed(&tw) - CONFIG_ANCHOR_ANT_DLY_DTU;
	mm = (int32_t)(((int64_t)tof * MM_PER_DTU_NUM) / MM_PER_DTU_DEN);

	if (mm < ANCHOR_MIN_MM || mm > ANCHOR_MAX_MM) {
		g_st.rejected++;
		return -ERANGE;
	}

	{
		/* Read HERE, after the round and before anything re-arms RX, because
		 * the CIA registers still describe the FINAL reception at this point.
		 * One SPI read of a few dozen bytes with no deadline pending: the
		 * round is over, the next POLL is ~210 ms away, and this is not the
		 * accumulator read that stops the CDK ranging altogether (RESULTS.md
		 * Result 7) -- that one is 64 taps and this one is a handful of
		 * registers. */
		dwt_rxdiag_t diag;

		dwt_readdiagnostics(&diag);
		g_st.last_fp_index = (uint32_t)diag.ipatovFpIndex;
		g_st.last_xtal_offset = (int32_t)diag.xtalOffset;
	}

	g_st.ranges++;
	g_st.last_raw_mm = mm;
	/* The median is what leaves this function. The raw value stays in the stats
	 * so the half-chip slip remains observable after the filter hides it. */
	mm = median_push(mm);
	g_st.last_mm = mm;

#if IS_ENABLED(CONFIG_ANCHOR_LOG_EACH_RANGE)
	/* The periodic report samples one range in ~450, which is why the 20-minute
	 * run produced 70 observations from 7,208 ranges. This is the population. */
	LOG_INF("RANGE mm=%d raw=%d fp=%u.%02u xtal=%d", (int)mm,
		(int)g_st.last_raw_mm,
		(unsigned int)(g_st.last_fp_index >> FP_INDEX_FRAC_BITS),
		(unsigned int)(((g_st.last_fp_index & FP_INDEX_FRAC_MASK) * 100u) /
			       (FP_INDEX_FRAC_MASK + 1u)),
		(int)g_st.last_xtal_offset);
#endif
	*mm_out = mm;
	*seq_out = seq;
	return 0;
}

void anchor_twr_stats(struct anchor_twr_stats *out)
{
	if (out != NULL) {
		*out = g_st;
	}
}
