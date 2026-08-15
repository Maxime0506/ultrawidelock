/* SPDX-License-Identifier: ISC */

/*
 * OpenThread's millisecond alarm.
 *
 * The stack keeps one alarm and re-arms it as its own timer list changes, so
 * all this owes it is a single deadline and a flag. The deadline is carried by
 * the port's delayable work rather than a FreeRTOS software timer, because the
 * callback only records the expiry and wakes the OpenThread task: the work
 * queue already exists, and a second timer service would buy nothing.
 *
 * The microsecond alarm is deliberately absent. It exists only when
 * OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE is set, which upstream defaults
 * to 0 and which this product does not turn on: it is a receiver-on MED with
 * no CSL and no network time sync, and nothing else asks for microsecond
 * timers. Adding it would mean shipping a second timer with no caller.
 */
#include <ultrawidelock_freertos_openthread.h>

#include <stdbool.h>
#include <stdint.h>

#include <ultrawidelock_freertos_platform.h>
#include <ultrawidelock_osal.h>

#include <openthread/platform/alarm-milli.h>

#define ALARM_MS_PER_SECOND 1000
#define ALARM_US_PER_MS 1000

static struct ultrawidelock_dwork s_alarm;
static bool s_alarm_ready;
static volatile bool s_fired;

/*
 * The deadline is recorded and the OpenThread task woken; the stack's own
 * callback runs from that task, under the API lock, in
 * ultrawidelock_freertos_openthread_alarm_process().
 */
static void alarm_expired(struct ultrawidelock_dwork *dwork)
{
	(void)dwork;
	s_fired = true;
	ultrawidelock_freertos_openthread_wake();
}

static void alarm_ready(void)
{
	if (!s_alarm_ready) {
		ultrawidelock_dwork_init(&s_alarm, alarm_expired);
		s_alarm_ready = true;
	}
}

uint32_t otPlatAlarmMilliGetNow(void)
{
	/*
	 * OpenThread's alarm clock is 32-bit and every comparison against it is
	 * written to survive the wrap, so truncating here is the contract rather
	 * than a loss.
	 */
	return (uint32_t)(ultrawidelock_freertos_uptime_us() / (int64_t)ALARM_US_PER_MS);
}

void otPlatAlarmMilliStartAt(otInstance *instance, uint32_t t0, uint32_t dt)
{
	int32_t delta;

	(void)instance;
	alarm_ready();

	/*
	 * The subtraction is deliberately done in unsigned 32-bit and only then
	 * read as signed, so a deadline that straddles the clock wrap still
	 * measures as a small positive delta.
	 */
	delta = (int32_t)(t0 + dt - otPlatAlarmMilliGetNow());

	if (delta > 0) {
		(void)ultrawidelock_dwork_reschedule(&s_alarm, delta);
		return;
	}

	/*
	 * Already due. Recording it here rather than scheduling a zero delay
	 * keeps the deadline from waiting on a work queue pass it does not need,
	 * and matches what the stack expects: a deadline in the past has fired.
	 */
	(void)ultrawidelock_dwork_cancel(&s_alarm);
	alarm_expired(&s_alarm);
}

void otPlatAlarmMilliStop(otInstance *instance)
{
	(void)instance;
	alarm_ready();
	(void)ultrawidelock_dwork_cancel(&s_alarm);
	s_fired = false;
}

/*
 * Called from the OpenThread task with the API lock held, so the stack may
 * re-arm the alarm from inside its own callback. The flag is cleared first for
 * exactly that reason.
 */
void ultrawidelock_freertos_openthread_alarm_process(otInstance *instance)
{
	if (!s_fired) {
		return;
	}
	s_fired = false;
	otPlatAlarmMilliFired(instance);
}
