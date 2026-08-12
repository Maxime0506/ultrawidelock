/*
 * Temporary. Delete this file when the radio layer joins the build graph.
 *
 * ports/freertos-nrf52833/board/startup_freertos.c routes five vectors --
 * POWER_CLOCK, RADIO, TIMER0, RTC0, and SWI5_EGU5 -- straight to MPSL through
 * the port's radio entry points, because peripherals.yml says MPSL owns them.
 * Those entry points live in radio/radio_start_freertos.c, which is not linked
 * yet, so this image would not link without something to resolve them.
 *
 * They are defined here rather than made weak in the startup file on purpose. A
 * weak vector would let a production image link with the radio silently absent,
 * and it would put a null check in a priority-zero interrupt handler. A
 * deliberately ugly file in the application, which the link fails without and
 * which is deleted in one commit, keeps the cost visible instead.
 *
 * Each of these is fatal rather than empty: if one is ever reached, the
 * peripheral was started by something that has no owner, and that is worth
 * knowing about immediately.
 */
#include <woz_freertos_platform.h>

void woz_freertos_radio_power_clock_isr(void)
{
	woz_freertos_fatal("CLOCK interrupt with no radio layer linked");
}

void woz_freertos_radio_radio_isr(void)
{
	woz_freertos_fatal("RADIO interrupt with no radio layer linked");
}

void woz_freertos_radio_timer0_isr(void)
{
	woz_freertos_fatal("TIMER0 interrupt with no radio layer linked");
}

void woz_freertos_radio_rtc0_isr(void)
{
	woz_freertos_fatal("RTC0 interrupt with no radio layer linked");
}

void woz_freertos_radio_low_priority_isr(void)
{
	woz_freertos_fatal("SWI5_EGU5 interrupt with no radio layer linked");
}
