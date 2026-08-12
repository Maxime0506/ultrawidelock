/*
 * OpenThread's entropy contract, reproduced so the host build does not need the
 * pinned tree. scripts/freertos-radio-source-check.sh asserts the name and the
 * error codes still match upstream.
 */
#ifndef TEST_OPENTHREAD_ENTROPY_H
#define TEST_OPENTHREAD_ENTROPY_H

#include <stdint.h>

#include <openthread/error.h>

otError otPlatEntropyGet(uint8_t *output, uint16_t length);

#endif /* TEST_OPENTHREAD_ENTROPY_H */
