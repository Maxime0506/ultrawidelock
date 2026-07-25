/** @file uwb_cirdiag.c — CIA RX-diagnostics latch + [ALAB] emitter (channel-impulse Stage 0/1).
 *
 * Split the work across the two contexts the ALAB contract demands: the RX callback only
 * latches registers into a snapshot (uwb_cirdiag_capture, plain stores + one SPI read), and a
 * task-side uwb_cirdiag_flush formats/prints the line. On the nRF the flush runs on the
 * sysworkq (uwb_rxdiag.c submits it); on the ESP32 the pinned ISR-service task calls it after
 * its IRQ drain loop, so capture and flush are sequential there. A seqlock covers the
 * one real race (nRF: a new capture preempting a flush mid-copy): torn snapshots are dropped,
 * the next reception re-latches.
 *
 * Stage 1 adds an independently-armed windowed-CIR dump: when armed, capture also reads a
 * fixed window of Ipatov complex taps centred on the first-path index into the snapshot. The
 * taps are NOT printed on the RX/flush path — a full window is ~64 serial lines per reception,
 * enough blocking UART to overrun the ranging slot and stall a live walk-up. Instead flush
 * appends each window to a small RAM ring (the last CIRDIAG_RING_RECS receptions), and the taps
 * are drained to `ev=uwb.cir` lines only when the dump is disarmed (uwb_cirdiag_dump_set_enabled
 * (false)) — that runs in console/task context after the walk-up, so the unlock is unaffected
 * while capturing. Deferring the printing was necessary but not sufficient: the window READ is
 * itself too long to sit inside a live ranging block, where the responder still owes a POLL or
 * Final reception. The shims pass that down as deadline_pending and the window is taken only on
 * the Final. Nor was that sufficient: the accumulator cannot be read at all while the receiver
 * is up, and the shim re-arms an SP0 listen the moment the Final is serviced, so the read has to
 * happen BEFORE that (the shims gate it on ccc_shim_rx_awaiting_final). Doing it on every block
 * then cost every range, so uwb_cirdiag_window_due decimates it to one Final in
 * CIRDIAG_CIR_EVERY.
 */

#include "uwb_cirdiag.h"

#include <stdint.h>
#include <string.h>

#include <deca_device_api.h>

#include "woz_log.h"  /* woz_printf — platform print, same sink as the [ALAB] trace */
#include "woz_port.h" /* woz_uptime_us — the [ALAB] timebase */

/** @brief SYS_CFG + STS-packet-config (CP_SPC): the mode of THIS reception (0=SP0..3=SP3).
 * Same trick as uwb_rxdiag.c's RXDIAG_CP_SPC, duplicated to keep this unit freestanding. */
#define CIRDIAG_SYS_CFG   0x10UL
#define CIRDIAG_CP_SPC(v) (unsigned)(((v) >> 12) & 0x3u)

/** @brief Windowed-CIR dump width, in complex taps, centred on the first-path index. 64 is
 * enough to carry the leading edge + early multipath that separates inside/outside a door,
 * while staying cheap on serial (~64 lines) and RAM (256 B in DWT_CIR_READ_MID: 1 word/tap). */
#define CIRDIAG_CIR_WIN 64u

/** @brief Take a window on one Final in every N. The read is the most expensive thing this unit
 * does — the driver walks the accumulator in CHUNK_CIR_NB_SAMP-sample chunks, three SPI
 * transactions each — and doing it on every block cost every range of the walk-up (bench run 5:
 * 16/16 windows came back as real CIR, and not one block produced a distance). Sampling every
 * fourth block still fills the ring across an approach while leaving three blocks in four
 * untouched. */
#define CIRDIAG_CIR_EVERY 4u

/** @brief ipatovFpIndex is Q10.6 (6 fractional bits); the integer sample index is the high bits. */
#define CIRDIAG_FP_FRAC_BITS 6u

/** @brief CLK_CTRL_ID. dwt_readcir ORs the ACC_MCLK_EN|ACC_CLK_EN bits in here on every call and
 * never clears them; the probe reads it back so a failed force shows up as data, not inference. */
#define CIRDIAG_CLK_CTRL 0x110004UL

/** @brief Taps per probe pass. Small enough that three passes at different offsets stay cheap;
 * the point is comparing them, not the width. */
#define CIRDIAG_PROBE_TAPS 8u

/** @brief Runtime arm state (console-toggled; OFF at boot). */
static volatile bool g_on;

/** @brief Windowed-CIR dump arm (independent of g_on; OFF at boot). */
static volatile bool g_dump;

/** @brief Chip-side CIA diagnostic logging enabled (lazily, on the RX path). */
static bool g_cia_armed;

/** @brief Latched snapshot of the most recent reception (latest wins). */
static dwt_rxdiag_t g_diag;
static int64_t g_t_us;
static uint32_t g_status;
static uint16_t g_len;
static unsigned g_sp;
static int16_t g_sts_qual;
static int32_t g_sts_qual_ret;
static uint16_t g_sts_stat;
static int32_t g_sts_stat_ret;
static uint32_t g_n;

/** @brief Windowed-CIR snapshot: DWT_CIR_READ_MID packs each tap as two int16 (real, imag) in
 * one word, so g_cir doubles as an int16[2*WIN] pair array. g_cir_base is the absolute Ipatov
 * sample index of tap 0; g_cir_have gates emission (false if dump disarmed or the read failed). */
static uint32_t g_cir[CIRDIAG_CIR_WIN];
static uint16_t g_cir_base;
static bool g_cir_have;

/** @brief Absolute Ipatov sample index of tap 0 for a window centred on the latched first path,
 * clamped into the valid accumulator span [0, DWT_CIR_LEN_IP_PRF64 - WIN]. */
static uint16_t cirdiag_window_base(void)
{
	uint16_t fp_int = (uint16_t)(g_diag.ipatovFpIndex >> CIRDIAG_FP_FRAC_BITS);
	uint16_t base = (fp_int > (CIRDIAG_CIR_WIN / 2u)) ? (fp_int - CIRDIAG_CIR_WIN / 2u) : 0u;

	if ((uint32_t)base + CIRDIAG_CIR_WIN > (uint32_t)DWT_CIR_LEN_IP_PRF64) {
		base = (uint16_t)(DWT_CIR_LEN_IP_PRF64 - CIRDIAG_CIR_WIN);
	}
	return base;
}

/** @brief Seqlock around the snapshot: odd while the RX path writes; the flush copies only
 * between two equal even reads. */
static volatile uint32_t g_seq;
static volatile bool g_pending;

/** @brief Deferred-dump ring. flush() appends each armed reception's window here (a cheap memcpy,
 * no UART) instead of printing it on the ranging path; the taps are emitted only on disarm
 * (cirdiag_drain), off that path, so a live walk-up still unlocks while capturing. Overwrites
 * oldest, so it holds the last RECS receptions — the near-door end of an approach (~272 B/record).
 * Single-producer (flush) / single-consumer (drain-after-disarm): the drain runs only after
 * g_dump is cleared, so in the intended "walk up, then dump off" workflow no live flush races it.
 */
#define CIRDIAG_RING_RECS 16u
/**
 * Channel impulse response diagnostic record: timestamp (microseconds), sample count, base index,
 * and tap array (I/Q magnitude or signed values) for post-processing.
 */
struct cirdiag_rec {
	int64_t t_us;
	uint32_t n;
	uint16_t base;
	uint32_t taps[CIRDIAG_CIR_WIN];
};
static struct cirdiag_rec g_ring[CIRDIAG_RING_RECS];
static uint32_t g_ring_head;  /* next write slot (mod RECS) */
static uint32_t g_ring_count; /* valid records held (<= RECS) */

/** @brief Emit every buffered window as `ev=uwb.cir` lines (oldest first), then empty the ring.
 * Called on dump disarm, off the ranging path. Blocking: up to RECS*WIN serial lines. */
static void cirdiag_drain(void)
{
	uint32_t start = (g_ring_head + CIRDIAG_RING_RECS - g_ring_count) % CIRDIAG_RING_RECS;

	for (uint32_t k = 0; k < g_ring_count; k++) {
		const struct cirdiag_rec *r = &g_ring[(start + k) % CIRDIAG_RING_RECS];
		const int16_t *s = (const int16_t *)r->taps;

		for (unsigned i = 0; i < CIRDIAG_CIR_WIN; i++) {
			woz_printf("[ALAB] t=%lld ev=uwb.cir n=%u i=%u re=%d im=%d\n",
				   (long long)r->t_us, (unsigned)r->n, (unsigned)(r->base + i),
				   (int)s[2u * i], (int)s[2u * i + 1u]);
		}
	}
	g_ring_count = 0;
	g_ring_head = 0;
}

/**
 * Return the count of CIR windows currently in the ring buffer (0 to CIRDIAG_RING_RECS).
 */
uint32_t uwb_cirdiag_ring_count(void)
{
	return g_ring_count;
}

/**
 * Enable or disable CIA diagnostic capture globally.
 */
void uwb_cirdiag_set_enabled(bool on)
{
	g_on = on;
}

/**
 * Return true if CIA diagnostic capture is enabled.
 */
bool uwb_cirdiag_enabled(void)
{
	return g_on;
}

/**
 * Arm or disarm CIR window capture. When arming, also arms the summary diagnostics. When disarming,
 * drains all buffered windows to the console via ev=uwb.cir lines and clears the ring.
 */
void uwb_cirdiag_dump_set_enabled(bool on)
{
	/* The window read needs the summary path armed too (it supplies the first-path index and
	 * the lazy CIA-logging enable). Arming dump implies arming the summary; disarming dump
	 * leaves the summary as-is. Disarm is also the safe moment to drain the buffered windows:
	 * it runs in console/task context after the walk-up, so the burst of serial lines never
	 * touches the ranging path. Clear g_dump first so no further capture appends mid-drain. */
	if (on) {
		g_on = true;
		g_dump = true;
	} else {
		g_dump = false;
		cirdiag_drain();
	}
}

/**
 * Return true if CIR window dump is armed (enabled and CIA logging armed).
 */
bool uwb_cirdiag_dump_enabled(void)
{
	return g_dump;
}

/**
 * Capture one reception's CIR diagnostic snapshot: arm CIA logging on first RX, then latch the
 * status, frame length, STS quality/status, and (if window dump is enabled and the radio is idle) a
 * fixed-size Ipatov-centred CIR window. Returns true if capture succeeded; false on first RX or if
 * already pending. Seqlock-protected; safe to call from RX callback.
 */
bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending)
{
	if (!g_on) {
		return false;
	}
	if (!g_cia_armed) {
		/* First reception after arming: driver init leaves cia_enable_mask 0, i.e. the
		 * CIA_CONF diagnostic-off bit set and the IP/STS diag banks unpopulated — enable
		 * full logging (+ the MAX double-buffer copy set, in case a session runs
		 * double-buffered). Done here rather than in set_enabled so the console toggle is
		 * safe before the chip is probed: this path only runs inside a live RX callback.
		 * THIS reception was demodulated with logging still reduced, so skip it; the next
		 * one carries a fully populated bank. Sticky until chip reset — only configciadiag
		 * and the 16-bit antenna-delay field write CIA_CONF, so a later dwt_configure()
		 * does not undo it (hence never re-cleared on `off`). */
		dwt_configciadiag((uint8_t)(DW_CIA_DIAG_LOG_ALL | DW_CIA_DIAG_LOG_MAX));
		g_cia_armed = true;
		return false;
	}
	g_seq++; /* odd: writer active */
	g_t_us = woz_uptime_us();
	g_status = status;
	g_len = datalength;
	g_sp = CIRDIAG_CP_SPC(dwt_read_reg(CIRDIAG_SYS_CFG));
	dwt_readdiagnostics(&g_diag);
	g_sts_qual = 0;
	g_sts_qual_ret = dwt_readstsquality(&g_sts_qual, 0);
	g_sts_stat = 0;
	g_sts_stat_ret = dwt_readstsstatus(&g_sts_stat, 0);
	g_cir_have = false;
	if (g_dump && !deadline_pending) {
		/* Centre a fixed window on the integer first-path index, clamped into the valid
		 * Ipatov accumulator span [0, DWT_CIR_LEN_IP_PRF64 - WIN]. dwt_readcir forces the
		 * ACC clocks on itself; MID mode gives int16 real/imag with headroom for the early
		 * taps. Inside the seqlock bracket so the flush copies a consistent window.
		 * deadline_pending false means the caller has established the radio is idle —
		 * on the ranging path that is the Final, sampled before the shim re-arms. */
		uint16_t base = cirdiag_window_base();

		g_cir_base = base;
		g_cir_have = (dwt_readcir(g_cir, DWT_ACC_IDX_IP_M, base, CIRDIAG_CIR_WIN,
					  DWT_CIR_READ_MID) == DWT_SUCCESS);
	}
	g_n++;
	g_seq++; /* even: stable */
	g_pending = true;
	return true;
}

/** @brief Decimation tick: the shims call this once per Final, so it advances per ranging block. */
static uint32_t g_win_tick;

/**
 * Return true on every CIRDIAG_CIR_EVERY-th call if capture is enabled, CIA logging is armed, and
 * window dump is armed; used to throttle window reads during streaming.
 */
bool uwb_cirdiag_window_due(void)
{
	if (!g_on || !g_dump || !g_cia_armed) {
		return false;
	}
	return (g_win_tick++ % CIRDIAG_CIR_EVERY) == 0u;
}

/**
 * Diagnostic: read and print the CIR at four sample offsets (three distinct addresses plus one
 * repeat) to verify addressing and detect non-determinism. Requires CIA logging armed (one
 * reception taken). Outputs one pass in MID mode (int16 real/imag) and one in FULL mode (raw
 * 24-bit) at the base offset.
 */
void uwb_cirdiag_probe(void)
{
	/* Offsets to sample, in taps, relative to the window base. The first three are distinct
	 * accumulator addresses; the fourth repeats the first. Reading the same address twice
	 * separates "the offset is ignored" from "the read is non-deterministic": if passes 0..2
	 * agree the addressing is dead, and if 0 and 3 disagree the read is racing something. */
	static const uint16_t offs[] = {0u, CIRDIAG_PROBE_TAPS, 2u * CIRDIAG_PROBE_TAPS, 0u};
	/* Sized for DWT_CIR_READ_FULL, which writes 6 bytes per tap (2 words) — MID uses half. */
	uint32_t buf[2u * CIRDIAG_PROBE_TAPS];
	uint16_t base;

	if (!g_cia_armed) {
		woz_printf("cir.probe: not ready: the chip-side CIA enable happens on the "
			   "RX path, so arm the stream and take one reception first\n");
		return;
	}
	base = cirdiag_window_base();
	woz_printf("cir.probe: base=%u fp=%u clk=%lu\n", (unsigned)base,
		   (unsigned)g_diag.ipatovFpIndex, (unsigned long)dwt_read_reg(CIRDIAG_CLK_CTRL));

	for (unsigned p = 0; p < (sizeof(offs) / sizeof(offs[0])); p++) {
		const int16_t *s = (const int16_t *)buf;
		uint16_t at = (uint16_t)(base + offs[p]);
		int rc;

		memset(buf, 0, sizeof(buf));
		rc = dwt_readcir(buf, DWT_ACC_IDX_IP_M, at, CIRDIAG_PROBE_TAPS, DWT_CIR_READ_MID);
		woz_printf("cir.probe: pass=%u at=%u rc=%d clk=%lu\n", p, (unsigned)at, rc,
			   (unsigned long)dwt_read_reg(CIRDIAG_CLK_CTRL));
		for (unsigned i = 0; i < CIRDIAG_PROBE_TAPS; i++) {
			woz_printf("cir.probe:   p=%u i=%u re=%d im=%d\n", p, i, (int)s[2u * i],
				   (int)s[2u * i + 1u]);
		}
	}

	/* One FULL-mode pass at the base: 24-bit real/imag straight off the wire, before the
	 * sign-extend/shift/saturate that MID applies. A blob that saturates to +-32767 in MID is
	 * unreadable; the raw words identify which memory the read actually landed on. */
	memset(buf, 0, sizeof(buf));
	{
		const uint8_t *b = (const uint8_t *)buf;
		int rc = dwt_readcir(buf, DWT_ACC_IDX_IP_M, base, CIRDIAG_PROBE_TAPS,
				     DWT_CIR_READ_FULL);

		woz_printf("cir.probe: full at=%u rc=%d\n", (unsigned)base, rc);
		for (unsigned i = 0; i < CIRDIAG_PROBE_TAPS; i++) {
			const uint8_t *t = &b[6u * i];

			woz_printf("cir.probe:   full i=%u re24=%lu im24=%lu\n", i,
				   (unsigned long)((uint32_t)t[0] | ((uint32_t)t[1] << 8) |
						   ((uint32_t)t[2] << 16)),
				   (unsigned long)((uint32_t)t[3] | ((uint32_t)t[4] << 8) |
						   ((uint32_t)t[5] << 16)));
		}
	}
}

/**
 * Emit the pending CIR snapshot: write the summary line ([ALAB] ev=uwb.diag) with Ipatov and STS
 * peak/power/quality fields, and either defer the window to the ring buffer (if window dump is
 * enabled) or skip it. Retry up to 3 times if the seqlock detects concurrent capture. Idempotent.
 */
void uwb_cirdiag_flush(void)
{
	dwt_rxdiag_t d;
	int64_t t_us;
	uint32_t status, n;
	unsigned sp;
	uint16_t len, sts_stat, cir_base;
	int16_t sts_qual;
	int32_t sts_qual_ret, sts_stat_ret;
	bool cir_have;
	/* Flush is single-context per port (nRF sysworkq item / ESP32 isr task), never
	 * re-entrant, so a static scratch window keeps 256 B off the task stack. */
	static uint32_t cir_copy[CIRDIAG_CIR_WIN];

	if (!g_pending) {
		return;
	}
	g_pending = false;

	for (int tries = 0; tries < 3; tries++) {
		uint32_t s0 = g_seq;

		if ((s0 & 1u) != 0u) {
			continue;
		}
		memcpy(&d, &g_diag, sizeof(d));
		t_us = g_t_us;
		status = g_status;
		len = g_len;
		sp = g_sp;
		sts_qual = g_sts_qual;
		sts_qual_ret = g_sts_qual_ret;
		sts_stat = g_sts_stat;
		sts_stat_ret = g_sts_stat_ret;
		n = g_n;
		cir_have = g_cir_have;
		cir_base = g_cir_base;
		if (cir_have) {
			memcpy(cir_copy, g_cir, sizeof(cir_copy));
		}
		if (g_seq != s0) {
			continue; /* a capture landed mid-copy — retry */
		}
		/* One line, all-decimal for aliro_lab.py's k=v parser. Keys: n capture#,
		 * sp STS mode, len/st the callback frame info; ip.. Ipatov and s.. STS CIR
		 * diagnostics — fp first-path index (Q10.6), pk peak (idx[30:21]|amp[20:0]),
		 * pw channel power, f1..f3 first-path amplitudes, ac accumulated symbols;
		 * sq/sqr STS quality index + verdict, ss/ssr STS status bits + verdict,
		 * xtal remote crystal offset, cd1 CIA_DIAG_1. */
		woz_printf("[ALAB] t=%lld ev=uwb.diag n=%u sp=%u len=%u st=%u "
			   "ipfp=%u ippk=%u ippw=%u ipf1=%u ipf2=%u ipf3=%u ipac=%u "
			   "sfp=%u spk=%u spw=%u sf1=%u sf2=%u sf3=%u sac=%u "
			   "sq=%d sqr=%d ss=%u ssr=%d xtal=%d cd1=%u\n",
			   (long long)t_us, (unsigned)n, sp, (unsigned)len, (unsigned)status,
			   (unsigned)d.ipatovFpIndex, (unsigned)d.ipatovPeak,
			   (unsigned)d.ipatovPower, (unsigned)d.ipatovF1, (unsigned)d.ipatovF2,
			   (unsigned)d.ipatovF3, (unsigned)d.ipatovAccumCount,
			   (unsigned)d.stsFpIndex, (unsigned)d.stsPeak, (unsigned)d.stsPower,
			   (unsigned)d.stsF1, (unsigned)d.stsF2, (unsigned)d.stsF3,
			   (unsigned)d.stsAccumCount, (int)sts_qual, (int)sts_qual_ret,
			   (unsigned)sts_stat, (int)sts_stat_ret, (int)d.xtalOffset,
			   (unsigned)d.ciaDiag1);
		if (cir_have) {
			/* Deferred dump: park the window in the ring (cheap memcpy, no UART)
			 * instead of printing ~64 lines here — that print would stall ranging (this
			 * runs in the ISR-service task on the ESP32). cirdiag_drain() emits them on
			 * disarm. Overwrite-oldest keeps the last RECS receptions. Keyed to the
			 * summary line by n; re/im are the int16 real/imag parts
			 * (DWT_CIR_READ_MID), grouped and magnitude-computed offline by
			 * aliro_lab.py. */
			struct cirdiag_rec *r = &g_ring[g_ring_head];

			r->t_us = t_us;
			r->n = n;
			r->base = cir_base;
			memcpy(r->taps, cir_copy, sizeof(r->taps));
			g_ring_head = (g_ring_head + 1u) % CIRDIAG_RING_RECS;
			if (g_ring_count < CIRDIAG_RING_RECS) {
				g_ring_count++;
			}
		}
		return;
	}
	/* Persistently torn — drop this snapshot; the next reception re-latches. */
}
