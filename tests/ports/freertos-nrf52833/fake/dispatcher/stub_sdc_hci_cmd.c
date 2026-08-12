#include "stub_sdc_hci_cmd.h"

#include <stdio.h>
#include <string.h>

#include <nrf_errno.h>
#include <sdc_hci.h>

#define WOZ_STUB_MAX 64

static struct {
	const char *name;
	unsigned calls;
} s_records[WOZ_STUB_MAX];
static unsigned s_used;
static unsigned s_total;

uint8_t woz_stub_record(const char *name)
{
	unsigned i;

	s_total++;
	for (i = 0; i < s_used; i++) {
		if (strcmp(s_records[i].name, name) == 0) {
			s_records[i].calls++;
			return 0;
		}
	}
	if (s_used == WOZ_STUB_MAX) {
		printf("  FAIL stub table is full at %s\n", name);
		return 0;
	}
	s_records[s_used].name = name;
	s_records[s_used].calls = 1;
	s_used++;
	return 0;
}

unsigned woz_stub_calls(const char *name)
{
	unsigned i;

	for (i = 0; i < s_used; i++) {
		if (strcmp(s_records[i].name, name) == 0) {
			return s_records[i].calls;
		}
	}
	return 0;
}

unsigned woz_stub_total(void)
{
	return s_total;
}

void woz_stub_reset(void)
{
	s_used = 0;
	s_total = 0;
}

/*
 * The controller output queue is empty for the whole test: everything the
 * dispatcher answers itself has to come back through its own staging buffer.
 */
int32_t sdc_hci_get(uint8_t *p_msg_out, uint8_t *p_msg_type_out)
{
	(void)p_msg_out;
	(void)p_msg_type_out;
	woz_stub_record("sdc_hci_get");
	return -NRF_EAGAIN;
}
