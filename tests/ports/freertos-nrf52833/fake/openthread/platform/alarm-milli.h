/*
 * OpenThread's millisecond alarm contract, as far as the port implements and
 * calls it. Reproduced rather than reached for, so the host build does not need
 * the pinned OpenThread tree; scripts/freertos-radio-source-check.sh asserts
 * these four names still exist upstream.
 */
#ifndef TEST_OPENTHREAD_ALARM_MILLI_H
#define TEST_OPENTHREAD_ALARM_MILLI_H

#include <stdint.h>

#include <openthread/tasklet.h>

void otPlatAlarmMilliStartAt(otInstance *instance, uint32_t t0, uint32_t dt);
void otPlatAlarmMilliStop(otInstance *instance);
uint32_t otPlatAlarmMilliGetNow(void);
extern void otPlatAlarmMilliFired(otInstance *instance);

#endif /* TEST_OPENTHREAD_ALARM_MILLI_H */
