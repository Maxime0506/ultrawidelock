/*
 * OpenThread's millisecond alarm. The interesting parts are not that a timer
 * fires but that the stack's own conventions survive: a deadline already past
 * fires without waiting, a deadline that straddles the 32-bit clock wrap is
 * still read as being in the future, and the alarm can be re-armed from inside
 * its own callback.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <openthread/platform/alarm-milli.h>
#include <woz_freertos_openthread.h>
#include <woz_osal.h>

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

/* The clock the alarm reads, under the test's control. */
static int64_t g_uptime_us;

int64_t woz_freertos_uptime_us(void)
{
	return g_uptime_us;
}

/* The port's delayable work, recorded rather than run. */
static struct woz_dwork *g_scheduled;
static int32_t g_delay_ms;
static unsigned g_schedule_calls;
static unsigned g_cancel_calls;
static woz_dwork_fn g_fn;

void woz_dwork_init(struct woz_dwork *dwork, woz_dwork_fn fn)
{
	g_fn = fn;
	(void)dwork;
}

int woz_dwork_reschedule(struct woz_dwork *dwork, int32_t delay_ms)
{
	g_scheduled = dwork;
	g_delay_ms = delay_ms;
	g_schedule_calls++;
	return 0;
}

int woz_dwork_cancel(struct woz_dwork *dwork)
{
	(void)dwork;
	g_scheduled = NULL;
	g_cancel_calls++;
	return 0;
}

/* Run the deadline the way the work queue would. */
static void expire(void)
{
	struct woz_dwork *due = g_scheduled;

	g_scheduled = NULL;
	if (due != NULL && g_fn != NULL) {
		g_fn(due);
	}
}

static unsigned g_wake_calls;

void woz_freertos_openthread_wake(void)
{
	g_wake_calls++;
}

static unsigned g_fired_calls;
static bool g_rearm_on_fire;

void otPlatAlarmMilliFired(otInstance *instance)
{
	g_fired_calls++;
	if (g_rearm_on_fire) {
		g_rearm_on_fire = false;
		/*
		 * Re-arms at a deadline already past, which is the case that
		 * catches a flag cleared after the callback instead of before:
		 * the clear would swallow the alarm the stack just set.
		 */
		otPlatAlarmMilliStartAt(instance, otPlatAlarmMilliGetNow() - 10u, 0);
	}
}

int main(void)
{
	otInstance *instance = (otInstance *)(uintptr_t)0x5eed;

	g_uptime_us = 1500000; /* 1500 ms */
	CHECK("the alarm clock is the platform clock in milliseconds",
	      otPlatAlarmMilliGetNow() == 1500);

	otPlatAlarmMilliStartAt(instance, 1500, 200);
	CHECK("a future deadline is scheduled for its remaining delay",
	      g_schedule_calls == 1 && g_delay_ms == 200 && g_scheduled != NULL);
	CHECK("and does not fire early", g_wake_calls == 0);

	woz_freertos_openthread_alarm_process(instance);
	CHECK("draining before the deadline delivers nothing", g_fired_calls == 0);

	g_uptime_us = 1700000;
	expire();
	CHECK("expiry wakes the OpenThread task rather than calling the stack",
	      g_wake_calls == 1 && g_fired_calls == 0);
	woz_freertos_openthread_alarm_process(instance);
	CHECK("the drain is what delivers it", g_fired_calls == 1);
	woz_freertos_openthread_alarm_process(instance);
	CHECK("and delivers it exactly once", g_fired_calls == 1);

	/* A deadline already past must not wait for a work queue pass. */
	otPlatAlarmMilliStartAt(instance, 1000, 100);
	CHECK("a deadline already past is recorded without scheduling",
	      g_schedule_calls == 1 && g_wake_calls == 2);
	woz_freertos_openthread_alarm_process(instance);
	CHECK("and is delivered on the next drain", g_fired_calls == 2);

	/*
	 * The stack re-arms the alarm from inside its own callback. Clearing the
	 * flag before the callback is what keeps that re-arm from being lost.
	 */
	g_rearm_on_fire = true;
	otPlatAlarmMilliStartAt(instance, otPlatAlarmMilliGetNow(), 10);
	expire();
	woz_freertos_openthread_alarm_process(instance);
	CHECK("re-arming from inside the callback survives the drain",
	      g_fired_calls == 3);
	woz_freertos_openthread_alarm_process(instance);
	CHECK("and the re-armed deadline is delivered on the next drain",
	      g_fired_calls == 4);

	/*
	 * The clock is 32 bits and wraps every 49.7 days. A deadline just past
	 * the wrap is still in the future, and unsigned arithmetic is what says
	 * so; a signed comparison would call it 49 days in the past.
	 */
	g_uptime_us = (int64_t)0xfffffe00 * 1000;
	otPlatAlarmMilliStartAt(instance, 0xfffffe00u, 0x400);
	CHECK("a deadline across the clock wrap is still in the future",
	      g_delay_ms == 0x400 && g_scheduled != NULL);

	otPlatAlarmMilliStop(instance);
	CHECK("stopping cancels the deadline", g_scheduled == NULL);
	woz_freertos_openthread_alarm_process(instance);
	CHECK("and drops a deadline that had already expired", g_fired_calls == 4);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
