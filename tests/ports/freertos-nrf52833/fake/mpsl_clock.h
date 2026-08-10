/* Subset of the pinned nrfxlib mpsl_clock.h. */
#ifndef TEST_MPSL_CLOCK_H
#define TEST_MPSL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum MPSL_CLOCK_LF_SRC {
	MPSL_CLOCK_LF_SRC_RC = 0,
	MPSL_CLOCK_LF_SRC_XTAL = 1,
	MPSL_CLOCK_LF_SRC_SYNTH = 2,
};

#define MPSL_RECOMMENDED_RC_CTIV        16
#define MPSL_RECOMMENDED_RC_TEMP_CTIV   2
#define MPSL_DEFAULT_CLOCK_ACCURACY_PPM 250

typedef struct {
	uint8_t source;
	uint8_t rc_ctiv;
	uint8_t rc_temp_ctiv;
	uint16_t accuracy_ppm;
	bool skip_wait_lfclk_started;
} mpsl_clock_lfclk_cfg_t;

#endif /* TEST_MPSL_CLOCK_H */
