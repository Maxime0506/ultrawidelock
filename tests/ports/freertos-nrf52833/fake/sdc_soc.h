/* Subset of the pinned nrfxlib sdc_soc.h. */
#ifndef TEST_SDC_SOC_H
#define TEST_SDC_SOC_H

#include <stdint.h>

typedef struct {
	void (*rand_poll)(uint8_t *p_buff, uint8_t length);
} sdc_rand_source_t;

int32_t sdc_rand_source_register(const sdc_rand_source_t *rand_source);

#endif /* TEST_SDC_SOC_H */
