/*
 * MPSL's temperature reading, in quarter-degree steps. MPSL owns the TEMP
 * peripheral, so this is the only way to ask.
 */
#ifndef TEST_MPSL_TEMP_H
#define TEST_MPSL_TEMP_H

#include <stdint.h>

int32_t mpsl_temperature_get(void);

/* Test control: what the next reading answers, and how many were taken. */
void fake_mpsl_temperature_set(int32_t quarter_degrees);
unsigned fake_mpsl_temperature_reads(void);
void fake_mpsl_temperature_reset(void);

#endif /* TEST_MPSL_TEMP_H */
