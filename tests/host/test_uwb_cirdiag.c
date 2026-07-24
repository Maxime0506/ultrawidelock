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
	T_OK("disarmed capture false", !uwb_cirdiag_capture(0x1u, 12u));
	T_EQ("disarmed no CIA write", drvfake.configciadiag_calls, 0);

	/* Arm summary: the first capture lazily enables CIA logging and skips
	 * itself; the next one latches. */
	uwb_cirdiag_set_enabled(true);
	T_OK("armed enabled", uwb_cirdiag_enabled());
	T_OK("lazy-arm capture skipped", !uwb_cirdiag_capture(0x1u, 12u));
	T_EQ("CIA logging enabled once", drvfake.configciadiag_calls, 1);
	T_OK("second capture latches", uwb_cirdiag_capture(0x1u, 12u));
	uwb_cirdiag_flush(); /* covers the seqlock copy + summary emit */

	/* Summary armed but dump off: no CIR read yet. */
	T_EQ("no CIR read without dump", drvfake.readcir_calls, 0);

	/* Dump arm implies summary; window centres on the integer first path
	 * (fp_int = ipatovFpIndex >> 6), base = fp_int - WIN/2 when it fits. */
	uwb_cirdiag_dump_set_enabled(true);
	T_OK("dump enabled", uwb_cirdiag_dump_enabled());
	drvfake.diag_fp = Q(200);
	(void)uwb_cirdiag_capture(0x1u, 12u);
	T_EQ("cir read issued", drvfake.readcir_calls, 1);
	T_EQ("window width", drvfake.last_cir_num, WIN);
	T_EQ("centred base", drvfake.last_cir_base, 200 - WIN / 2); /* 168 */
	uwb_cirdiag_flush(); /* dump armed: appends the window to the ring, no inline print */
	T_EQ("one window buffered", uwb_cirdiag_ring_count(), 1);

	/* First path near 0 -> base clamps to 0. */
	drvfake.diag_fp = Q(1);
	(void)uwb_cirdiag_capture(0x1u, 12u);
	T_EQ("low base clamps to 0", drvfake.last_cir_base, 0);

	/* First path near the end -> base clamps to IP_LEN - WIN. */
	drvfake.diag_fp = Q(1010);
	(void)uwb_cirdiag_capture(0x1u, 12u);
	T_EQ("high base clamps", drvfake.last_cir_base, IP_LEN - WIN); /* 952 */

	/* Fill past capacity: the ring keeps only the last RECS windows (overwrite oldest). */
	for (int i = 0; i < RECS + 8; i++) {
		drvfake.diag_fp = Q(200);
		(void)uwb_cirdiag_capture(0x1u, 12u);
		uwb_cirdiag_flush();
	}
	T_EQ("ring caps at capacity", uwb_cirdiag_ring_count(), RECS);

	/* Disarm dump: drains the ring off the ranging path, empties it; summary stays armed. */
	unsigned before = drvfake.readcir_calls;

	uwb_cirdiag_dump_set_enabled(false);
	T_OK("dump disarmed", !uwb_cirdiag_dump_enabled());
	T_OK("summary still armed", uwb_cirdiag_enabled());
	T_EQ("ring drained empty", uwb_cirdiag_ring_count(), 0);
	(void)uwb_cirdiag_capture(0x1u, 12u);
	T_EQ("no CIR read when dump off", drvfake.readcir_calls, before);

	/* Flush with nothing pending is a safe no-op. */
	uwb_cirdiag_flush();
	uwb_cirdiag_flush();

	uwb_cirdiag_set_enabled(false);
}
