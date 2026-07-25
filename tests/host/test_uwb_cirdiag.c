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
#define SUB     8
#define IP_LEN  1016
#define RECS    16
#define Q(fp)   ((uint16_t)((unsigned)(fp) << 6))

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
	uwb_cirdiag_probe();
	T_EQ("probe is a no-op before the CIA enable", drvfake.readcir_calls, 0);

	/* Arm summary: the first capture lazily enables CIA logging and skips
	 * itself; the next one latches. */
	uwb_cirdiag_set_enabled(true);
	T_OK("armed enabled", uwb_cirdiag_enabled());
	T_OK("lazy-arm capture skipped", !uwb_cirdiag_capture(0x1u, 12u, false));
	T_EQ("CIA logging enabled once", drvfake.configciadiag_calls, 1);
	T_OK("second capture latches", uwb_cirdiag_capture(0x1u, 12u, false));
	uwb_cirdiag_flush(); /* covers the seqlock copy + summary emit */

	/* Summary armed but dump off: no CIR read yet. */
	T_EQ("no CIR read without dump", drvfake.readcir_calls, 0);

	/* Dump arm implies summary; window centres on the integer first path
	 * (fp_int = ipatovFpIndex >> 6), base = fp_int - WIN/2 when it fits. The window is
	 * fetched as WIN/SUB contiguous sub-bursts so each accumulator read stays inside the
	 * ESP32's 64-byte non-DMA SPI limit — a single 64-tap call splits across bursts and
	 * came back non-physical on the bench. readcir_calls is zeroed before each capture so
	 * first_cir_base re-latches on that capture's opening sub-burst. */
	uwb_cirdiag_dump_set_enabled(true);
	T_OK("dump enabled", uwb_cirdiag_dump_enabled());
	drvfake.diag_fp = Q(200);
	drvfake.readcir_calls = 0;
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("window read as sub-bursts", drvfake.readcir_calls, WIN / SUB);
	T_EQ("sub-burst width", drvfake.last_cir_num, SUB);
	T_EQ("centred base", drvfake.first_cir_base, 200 - WIN / 2);              /* 168 */
	T_EQ("sub-bursts tile the window", drvfake.last_cir_base,
	     200 - WIN / 2 + WIN - SUB); /* 224 */
	uwb_cirdiag_flush(); /* dump armed: appends the window to the ring, no inline print */
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
		uwb_cirdiag_flush();
		T_EQ("no window buffered from a live block", uwb_cirdiag_ring_count(), 1);
	}

	/* Fill past capacity: the ring keeps only the last RECS windows (overwrite oldest). */
	for (int i = 0; i < RECS + 8; i++) {
		drvfake.diag_fp = Q(200);
		(void)uwb_cirdiag_capture(0x1u, 12u, false);
		uwb_cirdiag_flush();
	}
	T_EQ("ring caps at capacity", uwb_cirdiag_ring_count(), RECS);

	/* Disarm dump: drains the ring off the ranging path, empties it; summary stays armed. */
	unsigned before = drvfake.readcir_calls;

	uwb_cirdiag_dump_set_enabled(false);
	T_OK("dump disarmed", !uwb_cirdiag_dump_enabled());
	T_OK("summary still armed", uwb_cirdiag_enabled());
	T_EQ("ring drained empty", uwb_cirdiag_ring_count(), 0);
	(void)uwb_cirdiag_capture(0x1u, 12u, false);
	T_EQ("no CIR read when dump off", drvfake.readcir_calls, before);

	/* Flush with nothing pending is a safe no-op. */
	uwb_cirdiag_flush();
	uwb_cirdiag_flush();

	/* Probe: four MID passes (base, base+SUB, base+2*SUB, base again) then one FULL pass at
	 * the base, all SUB taps wide. The offsets are what make it diagnostic, so pin the first
	 * and last addresses rather than just the call count. */
	{
		drvfake.diag_fp = Q(200);
		drvfake.readcir_calls = 0;
		uwb_cirdiag_probe();
		T_EQ("probe reads 4 MID + 1 FULL", drvfake.readcir_calls, 5);
		T_EQ("probe burst width", drvfake.last_cir_num, SUB);
		T_EQ("probe starts at the window base", drvfake.first_cir_base, 200 - WIN / 2);
		T_EQ("probe ends back at the base", drvfake.last_cir_base, 200 - WIN / 2);
	}

	uwb_cirdiag_set_enabled(false);
}
