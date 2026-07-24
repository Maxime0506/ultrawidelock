/**
 * @file test_trace.c — the [WOZ_TRACE] helpers (modules/woz_uwb/src/facade/trace.h).
 *
 * Header-only code, so the suite is the TU: this file defines
 * CONFIG_WOZ_E2E_TRACE before including trace.h to instantiate the real hex
 * formatter (WOZ_TRACE itself maps to LOG_INF, a host no-op — nothing here
 * validates the log backend, only the hex prefix logic). trace_stub.c
 * instantiates the gated-off stub variant for the other branch.
 */
#include <string.h>

#define CONFIG_WOZ_E2E_TRACE 1
#include "trace.h"

#include "test.h"

/* Stub-variant probes from trace_stub.c (compiled without the config). */
extern const char *trace_stub_hex8(char buf[WOZ_TRACE_HEX8_LEN], const uint8_t *bytes, size_t len);
extern void trace_stub_emit(int v);

void test_trace(void)
{
	char buf[WOZ_TRACE_HEX8_LEN];
	static const uint8_t bytes[12] = {0x00, 0x1f, 0xa5, 0xff, 0x10, 0x32,
					  0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};

	t_group("woz_trace_hex8 (E2E variant)");
	memset(buf, 'x', sizeof(buf));
	T_OK("returns buf", woz_trace_hex8(buf, bytes, 0) == buf);
	T_OK("len 0 -> empty", buf[0] == '\0');
	T_OK("len 3", strcmp(woz_trace_hex8(buf, bytes, 3), "001fa5") == 0);
	T_OK("len 8 full", strcmp(woz_trace_hex8(buf, bytes, 8), "001fa5ff10325476") == 0);
	T_OK("len 12 capped at 8", strcmp(woz_trace_hex8(buf, bytes, 12), "001fa5ff10325476") == 0);
	T_OK("lowercase nybbles", strchr(woz_trace_hex8(buf, &bytes[3], 1), 'F') == NULL);

	t_group("emit macro type-checks");
	WOZ_TRACE("host.suite", "n=%u hex=%s", 7u, woz_trace_hex8(buf, bytes, 2));
	T_OK("live WOZ_TRACE returns", 1);

	t_group("gated-off stub variant");
	memset(buf, 'x', sizeof(buf));
	T_OK("stub returns buf", trace_stub_hex8(buf, bytes, 5) == buf);
	T_OK("stub yields empty string", buf[0] == '\0');
	trace_stub_emit(42);
	T_OK("stub WOZ_TRACE no-op returns", 1);
}
