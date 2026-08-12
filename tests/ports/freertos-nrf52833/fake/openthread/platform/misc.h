/*
 * OpenThread's reset and assertion contract, reproduced for the host build.
 */
#ifndef TEST_OPENTHREAD_MISC_H
#define TEST_OPENTHREAD_MISC_H

#include <openthread/error.h>
#include <openthread/tasklet.h>

typedef enum {
	OT_PLAT_RESET_REASON_POWER_ON = 0,
	OT_PLAT_RESET_REASON_EXTERNAL = 1,
	OT_PLAT_RESET_REASON_SOFTWARE = 2,
	OT_PLAT_RESET_REASON_FAULT = 3,
	OT_PLAT_RESET_REASON_CRASH = 4,
	OT_PLAT_RESET_REASON_ASSERT = 5,
	OT_PLAT_RESET_REASON_OTHER = 6,
	OT_PLAT_RESET_REASON_UNKNOWN = 7,
	OT_PLAT_RESET_REASON_WATCHDOG = 8,
} otPlatResetReason;

void otPlatReset(otInstance *instance);
otError otPlatResetToBootloader(otInstance *instance);
otPlatResetReason otPlatGetResetReason(otInstance *instance);
void otPlatAssertFail(const char *filename, int line);

#endif /* TEST_OPENTHREAD_MISC_H */
