#ifndef TEST_OPENTHREAD_TASKLET_H
#define TEST_OPENTHREAD_TASKLET_H

#include <stdbool.h>

typedef struct otInstance {
	unsigned test_id;
} otInstance;

bool otTaskletsArePending(otInstance *instance);
void otTaskletsProcess(otInstance *instance);
void otTaskletsSignalPending(otInstance *instance);

#endif /* TEST_OPENTHREAD_TASKLET_H */
