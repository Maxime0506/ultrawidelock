/* SPDX-License-Identifier: ISC */

/*
 * Starting and servicing Nordic's OpenThread radio platform.
 *
 * The platform itself is pinned vendor source compiled unmodified; see
 * ot_compat/ for the Zephyr surface it needs. What is left for this port is
 * the three joins Zephyr would otherwise make: bringing the platform up in the
 * right order, draining its pending work on the OpenThread task, and turning
 * its wake call into a FreeRTOS notification.
 */
#include <ultrawidelock_freertos_openthread.h>

#include <stdbool.h>

#include <ultrawidelock_freertos_platform.h>

#include <nrfx.h>

/*
 * The vendor platform's system-layer entry points. They are declared here
 * rather than included, because their upstream header belongs to OpenThread's
 * example platforms rather than to any API this port implements.
 */
void platformRadioInit(void);
void platformRadioProcess(otInstance *instance);

static bool s_started;

/*
 * The 802.15.4 driver arbitrates the radio through MPSL, so this must run
 * after ultrawidelock_freertos_radio_start(). It also has to run before the OpenThread
 * task starts, because the task drains the platform on every pass.
 */
int ultrawidelock_freertos_openthread_radio_start(void)
{
	if (s_started) {
		return 0;
	}

	platformRadioInit();
	s_started = true;
	return 0;
}

bool ultrawidelock_freertos_openthread_radio_started(void)
{
	return s_started;
}

/*
 * Called from the OpenThread task with the API lock held. Before the radio is
 * started there is nothing to drain, and calling in anyway would read the
 * platform's state before it was initialized.
 */
void ultrawidelock_freertos_openthread_process_drivers(otInstance *instance)
{
	/*
	 * The alarm needs no start of its own: it is armed by the stack, and a
	 * pass before anything armed it has nothing to deliver.
	 */
	ultrawidelock_freertos_openthread_alarm_process(instance);

	if (!s_started) {
		return;
	}
	platformRadioProcess(instance);
}

/*
 * The platform's wake call. Most of its callers are 802.15.4 driver callouts,
 * which run in interrupt context, so the context decides which FreeRTOS
 * notification path is legal: Cortex-M reports an active exception in IPSR,
 * and zero means thread mode.
 */
void otSysEventSignalPending(void)
{
	if (__get_IPSR() != 0u) {
		ultrawidelock_freertos_openthread_wake_from_isr();
		return;
	}
	ultrawidelock_freertos_openthread_wake();
}
