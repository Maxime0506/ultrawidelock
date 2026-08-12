/*
 * Host test for the walk-up latency trace (ultrawidelock_lat): one-shot mark
 * semantics, begin() reset, phase bounds, and the compiled-out no-op contract
 * when CONFIG_ULTRAWIDELOCK_LAT_TRACE is off. One test source, built both ways by
 * run.sh. The report lines land in the test output so the budget-line format
 * stays visible.
 */
#include <stdio.h>

#include "ultrawidelock_lat.h"

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

int main(void)
{
#ifdef CONFIG_ULTRAWIDELOCK_LAT_TRACE
	ultrawidelock_lat_report(); /* before any walk-up: the no-trace line, no crash */
	ultrawidelock_lat_begin();
	okc("begin stamps connect (re-mark is a no-op)",
	    ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_BLE_CONNECT) == 0);
	okc("first mark stamps", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_L2CAP_OPEN) == 1);
	okc("second mark is a no-op", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_L2CAP_OPEN) == 0);
	okc("out-of-range phase rejected", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_PHASE_COUNT) == 0);
	okc("last phase stamps", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_BOLT_DRIVEN) == 1);
	ultrawidelock_lat_report(); /* budget line; unreached phases print as "-" */
	ultrawidelock_lat_begin();
	okc("begin resets every mark", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_L2CAP_OPEN) == 1);
#else
	ultrawidelock_lat_begin();
	okc("gate off: mark is a no-op", ultrawidelock_lat_mark(ULTRAWIDELOCK_LAT_L2CAP_OPEN) == 0);
	ultrawidelock_lat_report(); /* compiled-out: must be silent and not crash */
#endif

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
