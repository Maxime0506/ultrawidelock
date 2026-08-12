// nRF5340DK on-target self-test for the Aliro device (initiator) EC path: a
// minimal Zephyr application that brings up the real PSA backend (nrf_security on
// CryptoCell), runs the same credential-auth crypto suite the host tests run, and
// prints PASS or FAIL to the DK console. It exists because the host suite proves
// the maths against a software curve only; this proves the same vectors on the
// silicon that will ship, and it caught a PSA import failure that no host run
// could see. Crypto only: no BLE, no UWB, no iPhone.
/*
 * nRF5340 entry for the Aliro device (initiator) EC self-test. Initialises the
 * PSA backend (nrf_security/CryptoCell), runs the same suite as the host test
 * (tests/shared/test_aliro_device.c) against the real P-256 curve, and
 * prints the verdict to the DK's UART console. Credential-auth crypto only; no
 * BLE, UWB or iPhone.
 */
#include <stdio.h>

#include <zephyr/kernel.h>

#include "ultrawidelock_prim.h"

int aliro_device_selftest(void);

/**
 * Run the on-target Aliro device EC self-test: initialize PSA, run the device self-test, and print
 * PASS or FAIL with return code.
 */
int main(void)
{
	printf("\n=== on-target Aliro device EC self-test (nRF5340, nrf_security PSA) ===\n");

	if (ultrawidelock_prim_init() != 0) {
		printf("\nON-TARGET RESULT: FAIL (PSA init)\n");
		return 0;
	}

	int rc = aliro_device_selftest();

	printf(rc == 0 ? "\nON-TARGET RESULT: PASS\n" : "\nON-TARGET RESULT: FAIL (rc=%d)\n", rc);
	return 0;
}
