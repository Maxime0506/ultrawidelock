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
 * fixed window of Ipatov complex taps centred on the first-path index into the snapshot, and
 * flush emits one `ev=uwb.cir` line per tap (absolute index + int16 real/imag). Separate arm
 * from the summary line because a full window is ~64 extra serial lines per reception — wanted
 * for offline analysis, not for a plain latency check.
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

/** @brief ipatovFpIndex is Q10.6 (6 fractional bits); the integer sample index is the high bits. */
#define CIRDIAG_FP_FRAC_BITS 6u

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

/** @brief Seqlock around the snapshot: odd while the RX path writes; the flush copies only
 * between two equal even reads. */
static volatile uint32_t g_seq;
static volatile bool g_pending;

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
	 * leaves the summary as-is. */
	if (on) {
		g_on = true;
	}
	g_dump = on;
}

bool uwb_cirdiag_dump_enabled(void)
{
	return g_dump;
}

bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength)
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
	if (g_dump) {
		/* Centre a fixed window on the integer first-path index, clamped into the valid
		 * Ipatov accumulator span [0, DWT_CIR_LEN_IP_PRF64 - WIN]. dwt_readcir forces the
		 * ACC clocks on itself; MID mode gives int16 real/imag with headroom for the early
		 * taps. Inside the seqlock bracket so the flush copies a consistent window. */
		uint16_t fp_int = (uint16_t)(g_diag.ipatovFpIndex >> CIRDIAG_FP_FRAC_BITS);
		uint16_t base =
			(fp_int > (CIRDIAG_CIR_WIN / 2u)) ? (fp_int - CIRDIAG_CIR_WIN / 2u) : 0u;

		if ((uint32_t)base + CIRDIAG_CIR_WIN > (uint32_t)DWT_CIR_LEN_IP_PRF64) {
			base = (uint16_t)(DWT_CIR_LEN_IP_PRF64 - CIRDIAG_CIR_WIN);
		}
		g_cir_base = base;
		g_cir_have = (dwt_readcir(g_cir, DWT_ACC_IDX_IP_M, base, CIRDIAG_CIR_WIN,
					  DWT_CIR_READ_MID) == DWT_SUCCESS);
	}
	g_n++;
	g_seq++; /* even: stable */
	g_pending = true;
	return true;
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
			/* One line per Ipatov tap, keyed to the summary line by n. i is the
			 * absolute accumulator index; re/im are the int16 real/imaginary parts
			 * (DWT_CIR_READ_MID). aliro_lab.py groups taps by n and computes
			 * magnitude offline. */
			const int16_t *s = (const int16_t *)cir_copy;

			for (unsigned i = 0; i < CIRDIAG_CIR_WIN; i++) {
				woz_printf("[ALAB] t=%lld ev=uwb.cir n=%u i=%u re=%d im=%d\n",
					   (long long)t_us, (unsigned)n, (unsigned)(cir_base + i),
					   (int)s[2u * i], (int)s[2u * i + 1u]);
			}
		}
		return;
	}
	/* Persistently torn — drop this snapshot; the next reception re-latches. */
}
