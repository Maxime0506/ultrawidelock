/**
 * @file drv_main.c — entry point for the driver test binary (host_test_drv).
 *
 * Runs the target-only driver/shell suites that cannot join the main host
 * binary: they compile the real uwb_min.c / uwb_isr.c / uwb_rxdiag.c /
 * uwb_selftest.c / aliro_shell.c, whose exported symbols the main binary
 * already fakes in dw_rx_stub.c. Everything below runs against recording
 * doubles (drvfake.c) — branch logic only, no hardware truth.
 */
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

extern volatile int woz_uwb_diag_on;

int main(void)
{
	/* Match run.sh's quiet convention: keep DIAGK off under `make test`. */
	if (getenv("WOZ_TEST_QUIET") != NULL) {
		woz_uwb_diag_on = 0;
	}

	test_uwb_min();
	test_uwb_isr();
	test_uwb_rxdiag();
	test_uwb_selftest();
	test_aliro_shell();

	if (t_fail > 0) {
		printf("  uwb-driver: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	printf("  uwb-driver: PASS (%d checks — fake radio, no hardware truth)\n", t_pass);
	return 0;
}
