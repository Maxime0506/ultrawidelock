/*
 * Host test for the walk-up latency trace (aliro_lat): one-shot mark
 * semantics, begin() reset, phase bounds, and the compiled-out no-op contract
 * when CONFIG_ALIRO_LAT_TRACE is off. One test source, built both ways by
 * run.sh. The report lines land in the test output so the budget-line format
 * stays visible.
 */
#include <stdio.h>

#include "aliro_lat.h"

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
#ifdef CONFIG_ALIRO_LAT_TRACE
	aliro_lat_report(); /* before any walk-up: the no-trace line, no crash */
	aliro_lat_begin();
	okc("begin stamps connect (re-mark is a no-op)",
	    aliro_lat_mark(ALIRO_LAT_BLE_CONNECT) == 0);
	okc("first mark stamps", aliro_lat_mark(ALIRO_LAT_L2CAP_OPEN) == 1);
	okc("second mark is a no-op", aliro_lat_mark(ALIRO_LAT_L2CAP_OPEN) == 0);
	okc("out-of-range phase rejected", aliro_lat_mark(ALIRO_LAT_PHASE_COUNT) == 0);
	okc("last phase stamps", aliro_lat_mark(ALIRO_LAT_BOLT_DRIVEN) == 1);
	aliro_lat_report(); /* budget line; unreached phases print as "-" */
	aliro_lat_begin();
	okc("begin resets every mark", aliro_lat_mark(ALIRO_LAT_L2CAP_OPEN) == 1);
#else
	aliro_lat_begin();
	okc("gate off: mark is a no-op", aliro_lat_mark(ALIRO_LAT_L2CAP_OPEN) == 0);
	aliro_lat_report(); /* compiled-out: must be silent and not crash */
#endif

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
