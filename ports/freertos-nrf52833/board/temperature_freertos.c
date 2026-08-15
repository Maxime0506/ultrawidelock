/* SPDX-License-Identifier: ISC */

/*
 * Die temperature, from MPSL rather than from the TEMP peripheral.
 *
 * peripherals.yml assigns TEMP to MPSL, which is not a preference: MPSL takes
 * its own temperature readings to calibrate the low-frequency clock, and the
 * TEMP peripheral answers one measurement at a time. A second reader driving
 * TASKS_START underneath it would corrupt a calibration that the whole radio
 * timebase rests on.
 *
 * MPSL reports quarter-degree steps. The nRF 802.15.4 driver, which is the only
 * consumer, wants whole degrees.
 */
#include <mpsl_temp.h>

#include <ultrawidelock_freertos_platform.h>

int8_t ultrawidelock_freertos_die_temperature_c(void)
{
	int32_t quarter_degrees = mpsl_temperature_get();

	/*
	 * Truncation toward zero, not rounding. The consumer uses this to pick a
	 * radio calibration band and a degree either way is inside the band's
	 * tolerance, so the simpler conversion is the honest one.
	 */
	return (int8_t)(quarter_degrees / 4);
}
