/**
 * @file main.c — experimental secondary UWB observer on nRF5340 + DWM3000EVB.
 *
 * Goal: determine whether a second DW3000 can usefully observe the stock
 * iPhone UWB exchange already driven by the primary DWM3001CDK lock.
 *
 * This image does NOT assume success. It:
 *   - brings up the DW3000
 *   - listens for energy / frames on the FiRa/CCC channel when configured
 *   - logs receive-power style diagnostics over RTT/UART
 *   - never logs session keys or long-lived credentials
 *   - never commands an unlock
 *
 * Independent secondary ranging to the iPhone is attempted only if the
 * protocol configuration is explicitly provided by a short-lived, local
 * experiment harness — not by reading production secrets from the lock.
 *
 * Build (requires workspace + DWM3000EVB shield overlay on the DK):
 *   make secondary-uwb-build
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sec_uwb, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("secondary-uwb experiment: nRF5340 + DWM3000EVB");
	LOG_INF("status=scaffold — passive iPhone UWB observe not yet proven");
	LOG_INF("unsupported until demonstrated: multi-anchor iPhone UWB ranging");
	LOG_INF("this node cannot unlock the door");

	for (;;) {
		k_sleep(K_SECONDS(5));
		LOG_INF("alive; awaiting experiment harness configuration");
	}
	return 0;
}
