#include <mpsl_temp.h>

static int32_t s_quarter_degrees;
static unsigned s_reads;

int32_t mpsl_temperature_get(void)
{
	s_reads++;
	return s_quarter_degrees;
}

void fake_mpsl_temperature_set(int32_t quarter_degrees)
{
	s_quarter_degrees = quarter_degrees;
}

unsigned fake_mpsl_temperature_reads(void)
{
	return s_reads;
}

void fake_mpsl_temperature_reset(void)
{
	s_quarter_degrees = 0;
	s_reads = 0;
}
