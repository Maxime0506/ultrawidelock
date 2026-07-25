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
 * the Final, which has the whole inter-block gap behind it (bench run 3: walk-up unlocks).
 * Nor was that sufficient on its own — the read must also be issued in CIRDIAG_CIR_SUBREAD-tap
 * pieces so no single accumulator burst exceeds the port's non-DMA SPI limit.
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

/** @brief Taps per dwt_readcir() call. The driver fetches 1 + 6*N raw bytes per accumulator
 * burst, so N=8 keeps each burst at 49 bytes — inside the ESP32's 64-byte non-DMA SPI limit,
 * which every other DW3000 read in the system also stays under. Asking for all 64 taps in one
 * call requests 97 bytes, which the port splits across two bursts; bench runs 2 and 3 returned
 * ~75 percent of those windows as a fixed non-physical pattern (identical samples at different
 * accumulator offsets, saturated at int16 limits). Must divide CIRDIAG_CIR_WIN. */
#define CIRDIAG_CIR_SUBREAD 8u

/** @brief ipatovFpIndex is Q10.6 (6 fractional bits); the integer sample index is the high bits. */
#define CIRDIAG_FP_FRAC_BITS 6u

/** @brief CLK_CTRL_ID. dwt_readcir ORs the ACC_MCLK_EN|ACC_CLK_EN bits in here on every call and
 * never clears them; the probe reads it back so a failed force shows up as data, not inference. */
#define CIRDIAG_CLK_CTRL 0x110004UL

/** @brief Taps per probe pass. Matches CIRDIAG_CIR_SUBREAD so the probe exercises the same
 * accumulator burst size the capture path uses. */
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

uint32_t uwb_cirdiag_ring_count(void)
{
	return g_ring_count;
}

void uwb_cirdiag_set_enabled(bool on)
{
	g_on = on;
}

bool uwb_cirdiag_enabled(void)
{
	return g_on;
}

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

bool uwb_cirdiag_dump_enabled(void)
{
	return g_dump;
}

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
		 * Taken only once the block owes no further radio event (see deadline_pending):
		 * this read is long enough to lose an armed POLL or Final — bench-proven to kill
		 * every range of the walk-up when it runs inside a live block. */
		uint16_t base = cirdiag_window_base();
		bool ok = true;

		g_cir_base = base;

		for (unsigned off = 0; ok && off < CIRDIAG_CIR_WIN; off += CIRDIAG_CIR_SUBREAD) {
			ok = (dwt_readcir(&g_cir[off], DWT_ACC_IDX_IP_M, (uint16_t)(base + off),
					  CIRDIAG_CIR_SUBREAD, DWT_CIR_READ_MID) == DWT_SUCCESS);
		}
		g_cir_have = ok;
	}
	g_n++;
	g_seq++; /* even: stable */
	g_pending = true;
	return true;
}

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
