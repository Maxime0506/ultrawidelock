/* Helper TU for test_trace.c: includes trace.h WITHOUT CONFIG_WOZ_E2E_TRACE
 * (the main host build's default), so the gated-off stub variant of
 * woz_trace_hex8 / WOZ_TRACE is instantiated and exercised too. The suite
 * itself (test_trace.c) defines the config macro and gets the real formatter;
 * the two static-inline variants coexist because each has internal linkage. */
#include <stddef.h>
#include <stdint.h>

#include "trace.h"

const char *trace_stub_hex8(char buf[WOZ_TRACE_HEX8_LEN], const uint8_t *bytes, size_t len)
{
	return woz_trace_hex8(buf, bytes, len);
}

void trace_stub_emit(int v)
{
	/* The if(0) no-op form must still type-check its arguments. */
	WOZ_TRACE("host.stub", "v=%d", v);
}
