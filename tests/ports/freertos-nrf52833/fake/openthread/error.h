/*
 * The OpenThread error codes the port returns. Values are upstream's, which
 * scripts/freertos-radio-source-check.sh checks.
 */
#ifndef TEST_OPENTHREAD_ERROR_H
#define TEST_OPENTHREAD_ERROR_H

typedef enum {
	OT_ERROR_NONE = 0,
	OT_ERROR_FAILED = 1,
	OT_ERROR_NO_BUFS = 3,
	OT_ERROR_INVALID_ARGS = 7,
	OT_ERROR_NOT_IMPLEMENTED = 12,
	OT_ERROR_NOT_FOUND = 23,
	OT_ERROR_NOT_CAPABLE = 27,
} otError;

#endif /* TEST_OPENTHREAD_ERROR_H */
