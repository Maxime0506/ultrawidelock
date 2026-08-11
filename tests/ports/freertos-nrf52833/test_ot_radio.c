/*
 * Starting and servicing the pinned OpenThread radio platform. The vendor
 * platform is stood in for by recording doubles here; what is under test is
 * the three joins the port owns, and in particular that neither the drain nor
 * the wake can reach the platform before it exists or from the wrong context.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fake_freertos.h"
#include "fake_nrf.h"

#include <nrfx.h>
#include <woz_freertos_openthread.h>

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                    \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                              \
			printf("  FAIL %s\n", (label));                                            \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                            \
		}                                                                                  \
	} while (0)

/* The vendor platform's system-layer entry points, recorded. */
static unsigned g_init_calls;
static unsigned g_process_calls;
static otInstance *g_process_instance;

void platformRadioInit(void)
{
	g_init_calls++;
}

void platformRadioProcess(otInstance *instance)
{
	g_process_calls++;
	g_process_instance = instance;
}

/* The task-side wake path this port already owns, recorded. */
static unsigned g_wake_calls;
static unsigned g_wake_isr_calls;

void woz_freertos_openthread_wake(void)
{
	g_wake_calls++;
}

void woz_freertos_openthread_wake_from_isr(void)
{
	g_wake_isr_calls++;
}

void otSysEventSignalPending(void);

int main(void)
{
	otInstance *instance = (otInstance *)(uintptr_t)0x5eed;

	fake_freertos_reset();
	fake_nrf_reset();
	fake_primask_reset();

	/*
	 * The task loop calls this on every pass, including the passes before
	 * the radio is up. Reading the platform's state then would read it
	 * uninitialized.
	 */
	woz_freertos_openthread_process_drivers(instance);
	CHECK("draining before the radio starts does not reach the platform",
	      g_process_calls == 0);
	CHECK("and the radio does not claim to be started",
	      !woz_freertos_openthread_radio_started());

	CHECK("starting the radio brings the platform up",
	      woz_freertos_openthread_radio_start() == 0 && g_init_calls == 1 &&
		      woz_freertos_openthread_radio_started());
	CHECK("starting twice does not initialize the platform twice",
	      woz_freertos_openthread_radio_start() == 0 && g_init_calls == 1);

	woz_freertos_openthread_process_drivers(instance);
	CHECK("draining now reaches the platform with the task's own instance",
	      g_process_calls == 1 && g_process_instance == instance);

	/*
	 * The platform signals pending work from wherever it noticed it. Most
	 * of those are 802.15.4 driver callouts, which run in an interrupt.
	 */
	otSysEventSignalPending();
	CHECK("a signal from the task wakes through the task path",
	      g_wake_calls == 1 && g_wake_isr_calls == 0);

	fake_ipsr_set(16);
	otSysEventSignalPending();
	fake_ipsr_set(0);
	CHECK("a signal from an interrupt wakes through the interrupt path",
	      g_wake_isr_calls == 1 && g_wake_calls == 1);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
