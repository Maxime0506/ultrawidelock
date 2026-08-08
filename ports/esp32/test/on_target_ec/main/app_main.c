/*
 * On-target entry for the Aliro device (initiator) EC self-test. Initialises the
 * PSA backend, runs the same suite as the host test (ports/esp32/test/
 * test_aliro_device.c) but against the real P-256 curve, prints the verdict, then
 * idles so the serial monitor can read it. Exercises only the credential-auth
 * crypto path; no BLE, UWB or iPhone is involved.
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aliro_prim.h"

int aliro_device_selftest(void);

void app_main(void)
{
	printf("\n=== on-target Aliro device EC self-test (real PSA P-256) ===\n");

	if (aliro_prim_init() != 0) {
		printf("\nON-TARGET RESULT: FAIL (PSA init)\n");
	} else {
		int rc = aliro_device_selftest();

		printf(rc == 0 ? "\nON-TARGET RESULT: PASS\n"
			       : "\nON-TARGET RESULT: FAIL (rc=%d)\n",
		       rc);
	}

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
