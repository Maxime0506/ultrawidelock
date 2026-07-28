/**
 * @file test_uwb_cirdiag.c — the CIA/CIR diagnostics latch (uwb_cirdiag.c)
 * driven directly through its public API against the drvfake radio doubles.
 * No hardware truth: dwt_readdiagnostics/readcir/configciadiag are recording
 * no-ops, so the checks pin the arm / lazy-CIA-enable / window-clamp branch
 * logic rather than any accumulator content.
 */
#include "drvfake.h"
#include "test.h"
#include "uwb_cirdiag.h"

/* Mirror of the firmware constants (uwb_cirdiag.c): 64-tap window over a
 * 1016-sample PRF64 Ipatov accumulator; ipatovFpIndex is Q10.6; RECS is the
 * deferred-dump ring depth (CIRDIAG_RING_RECS). */
#define WIN     64
#define SUB     8 /* CIRDIAG_PROBE_TAPS: the probe's per-pass burst width */
#define EVERY   4 /* CIRDIAG_CIR_EVERY: windows are taken on one Final in EVERY */
#define IP_LEN  1016
#define RECS    16
#define Q(fp)   ((uint16_t)((unsigned)(fp) << 6))

/* The flush emits the per-reception ev=uwb.diag summary line, and this suite
 * drives dozens of them while checking ring state rather than text. Muted at
 * the call so the checks around it still report. */
static void flush_quiet(void)
{
	t_mute();
	uwb_cirdiag_flush();
	t_unmute();
}

void test_uwb_cirdiag(void)
{
	drvfake_reset();

	/* Disarmed: capture is a no-op returning false, no CIA write. */
	uwb_cirdiag_set_enabled(false);
	uwb_cirdiag_dump_set_enabled(false);
	T_OK("disarmed enabled==false", !uwb_cirdiag_enabled());
	T_OK("disarmed capture false", !uwb_cirdiag_capture(0x1u, 12u, false));
	T_EQ("disarmed no CIA write", drvfake.configciadiag_calls, 0);

	/* Probe before the chip-side CIA enable has happened: refuses, touches no accumulator.
	 * Must run here — the enable is sticky, so this branch is unreachable once armed. */
	t_mute(); /* prints the "not ready" refusal */
	uwb_cirdiag_probe();
	t_unmute();
	T_EQ("probe is a no-op before the CIA enable", drvfake.readcir_calls, 0);

	/* Arm summary: the first capture lazily enables CIA logging and skips
	 * itself; the next one latches. */
	uwb_cirdiag_set_enabled(true);
	T_OK("armed enabled", uwb_cirdiag_enabled());
	T_OK("lazy-arm capture skipped", !uwb_cirdiag_capture(0x1u, 12u, false));
	T_EQ("CIA logging enabled once", drvfake.configciadiag_calls, 1);
	T_OK("second capture latches", uwb_cirdiag_capture(0x1u, 12u, false));
	flush_quiet(); /* covers the seqlock copy + summary emit */

	/* Summary armed but dump off: no CIR read yet. */
	T_EQ("no CIR read without dump", drvfake.readcir_calls, 0);

	/* Dump arm implies summary; window centres on the integer first path
	 * (fp_int = ipatovFpIndex >> 6), base = fp_int - WIN/2 when it fits. One dwt_readcir for
	 * the whole window: splitting it into 8-tap pieces was tried on the bench to dodge the
	 * ESP32's 64-byte non-DMA SPI limit and made things worse, because the real fault was
	 * reading while the receiver was up, not the burst size. readcir_calls is zeroed before
	 * each capture so first_cir_base re-latches on that capture. */
	uwb_cirdiag_dump_set_enabled(true);
	T_OK("dump enabled", uwb_cirdiag_dump_enabled());
	drvfake.diag_fp = Q(200);
	drvfake.readcir_calls = 0;
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("window read in one call", drvfake.readcir_calls, 1);
	T_EQ("full window width", drvfake.last_cir_num, WIN);
	T_EQ("centred base", drvfake.first_cir_base, 200 - WIN / 2); /* 168 */
	flush_quiet(); /* dump armed: appends the window to the ring, no inline print */
	T_EQ("one window buffered", uwb_cirdiag_ring_count(), 1);

	/* First path near 0 -> base clamps to 0. */
	drvfake.diag_fp = Q(1);
	drvfake.readcir_calls = 0;
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("low base clamps to 0", drvfake.first_cir_base, 0);

	/* First path near the end -> base clamps to IP_LEN - WIN. */
	drvfake.diag_fp = Q(1010);
	drvfake.readcir_calls = 0;
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("high base clamps", drvfake.first_cir_base, IP_LEN - WIN); /* 952 */

	/* Reception inside a live block (deadline_pending): a POLL or Final RX is armed behind it,
	 * so the long window read is skipped — the summary is still latched. Bench: an armed dump
	 * reading there lost every range of the walk-up. */
	{
		unsigned reads = drvfake.readcir_calls;

		drvfake.diag_fp = Q(200);
		T_OK("live block still latches summary", uwb_cirdiag_capture(0x1u, 12u, true));
		T_EQ("no CIR read inside a live block", drvfake.readcir_calls, reads);
		flush_quiet();
		T_EQ("no window buffered from a live block", uwb_cirdiag_ring_count(), 1);
	}

	/* Fill past capacity: the ring keeps only the last RECS windows (overwrite oldest). */
	for (int i = 0; i < RECS + 8; i++) {
		drvfake.diag_fp = Q(200);
		(void)uwb_cirdiag_capture(0x1u, 12u, false);
		flush_quiet();
	}
	T_EQ("ring caps at capacity", uwb_cirdiag_ring_count(), RECS);

	/* Disarm dump: drains the ring off the ranging path, empties it; summary stays armed. */
	unsigned before = drvfake.readcir_calls;

	/* The disarm drains RECS windows of WIN taps as serial lines. Muted: the
	 * check below is on the ring state, not on the text. */
	t_mute();
	uwb_cirdiag_dump_set_enabled(false);
	t_unmute();
	T_OK("dump disarmed", !uwb_cirdiag_dump_enabled());
	T_OK("summary still armed", uwb_cirdiag_enabled());
	T_EQ("ring drained empty", uwb_cirdiag_ring_count(), 0);
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("no CIR read when dump off", drvfake.readcir_calls, before);

	/* Flush with nothing pending is a safe no-op. */
	flush_quiet();
	flush_quiet();

	/* Decimation: window_due gates the expensive read to one Final in EVERY. It is
	 * side-effecting by contract (one call per Final), so count a full period. Reading every
	 * block returns real CIR but costs every range of the walk-up (bench run 5). */
	uwb_cirdiag_dump_set_enabled(true); /* the disarm above turned it off */
	{
		int due = 0;

		for (int i = 0; i < EVERY * 3; i++) {
			if (uwb_cirdiag_window_due()) {
				due++;
			}
		}
		T_EQ("window due once per period", due, 3);
	}
	t_mute(); /* second drain of the ring, same reason as above */
	uwb_cirdiag_dump_set_enabled(false);
	t_unmute();
	T_OK("window never due while the dump is disarmed", !uwb_cirdiag_window_due());
	uwb_cirdiag_dump_set_enabled(true);

	/* Probe: four MID passes (base, base+SUB, base+2*SUB, base again) then one FULL pass at
	 * the base, all SUB taps wide. The offsets are what make it diagnostic, so pin the first
	 * and last addresses rather than just the call count. */
	{
		drvfake.diag_fp = Q(200);
		drvfake.readcir_calls = 0;
		t_mute(); /* the probe prints every tap it reads */
		uwb_cirdiag_probe();
		t_unmute();
		T_EQ("probe reads 4 MID + 1 FULL", drvfake.readcir_calls, 5);
		T_EQ("probe burst width", drvfake.last_cir_num, SUB);
		T_EQ("probe starts at the window base", drvfake.first_cir_base, 200 - WIN / 2);
		T_EQ("probe ends back at the base", drvfake.last_cir_base, 200 - WIN / 2);
	}

	uwb_cirdiag_set_enabled(false);
}
