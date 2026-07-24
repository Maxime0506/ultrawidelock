/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * nRF5340 entry for the Aliro device (initiator) EC self-test. Initialises the
 * PSA backend (nrf_security/CryptoCell), runs the same suite as the host test
 * (ports/esp32/test/test_aliro_device.c) against the real P-256 curve, and
 * prints the verdict to the DK's UART console. Credential-auth crypto only; no
 * BLE, UWB or iPhone.
 */
#include <stdio.h>

#include <zephyr/kernel.h>

#include "aliro_prim.h"

int aliro_device_selftest(void);

int main(void)
{
	printf("\n=== on-target Aliro device EC self-test (nRF5340, nrf_security PSA) ===\n");

	if (aliro_prim_init() != 0) {
		printf("\nON-TARGET RESULT: FAIL (PSA init)\n");
		return 0;
	}

	int rc = aliro_device_selftest();

	printf(rc == 0 ? "\nON-TARGET RESULT: PASS\n" : "\nON-TARGET RESULT: FAIL (rc=%d)\n", rc);
	return 0;
}
