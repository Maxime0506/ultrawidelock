/**
 * @file test_woz_logfmt.c — the PRETTY console formatter + quiet-list on host.
 *
 * Drives woz_logfmt.c / woz_logquiet.c through the fake Zephyr logging surface
 * (tests/host/logfake/): init registration, the wrap-free 64-bit DWT
 * timestamp, the sec.ns line prefix, module tag colouring with the ERR/WRN
 * overrides, boot-banner "*** ... ***" stripping, inline hexdump payloads,
 * the 320-byte line cap, and the stock-renderer delegation for packageless
 * messages. The fake cbpprintf treats a package as the rendered string, so
 * nothing here validates Zephyr's package encoding — only our formatting.
 */
#include <stdio.h>
#include <string.h>

#include "logfake.h"
#include "test.h"

#include <cmsis_core.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_output.h>

/* SYS_INIT hooks surfaced by the fake <zephyr/init.h>. */
extern int (*const logfake_sys_init_woz_logfmt_init)(void);
extern int (*const logfake_sys_init_woz_logquiet_init)(void);

/* --- capture sink ----------------------------------------------------------- */
static uint8_t cap[1024];
static size_t cap_len;

static int cap_out(uint8_t *buf, size_t size, void *ctx)
{
	(void)ctx;
	memcpy(cap + cap_len, buf, size);
	cap_len += size;
	return (int)size;
}

static struct log_output_control_block cap_cb;
static const struct log_output cap_output = {cap_out, &cap_cb};

static void fmt(struct log_msg *msg, uint32_t flags)
{
	cap_len = 0;
	memset(cap, 0, sizeof(cap));
	logfake.formatter(&cap_output, msg, flags);
	cap[cap_len] = 0; /* NUL for strstr/strcmp; cap has headroom */
}

static struct log_msg mk(uint8_t level, int16_t sid, uint64_t ts, const char *text)
{
	struct log_msg m;

	memset(&m, 0, sizeof(m));
	m.level = level;
	m.source_id = sid;
	m.timestamp = ts;
	m.package = (uint8_t *)text;
	m.plen = strlen(text);
	return m;
}

void test_woz_logfmt(void)
{
	/* --- woz_logquiet: mute known sources, skip absent ones ------------- */
	t_group("logquiet");
	logfake_reset();
	logfake.source_name[0] = "chip";
	logfake.source_name[1] = "access_document"; /* "bt_adv" absent */
	T_EQ("quiet init rc", logfake_sys_init_woz_logquiet_init(), 0);
	T_EQ("quiet mutes present sources", logfake.filter_set_calls, 2);
	T_EQ("quiet sid chip", logfake.filter_sid[0], 0);
	T_EQ("quiet sid access_document", logfake.filter_sid[1], 1);
	T_EQ("quiet level NONE", (long)logfake.filter_level[0], LOG_LEVEL_NONE);

	/* --- init: clock fallback, registrations, backend sweep ------------- */
	t_group("logfmt init");
	logfake_reset();
	logfake.backend_count = 3;
	logfake.core_clock = 0; /* SystemCoreClock 0 -> 64 MHz fallback */
	T_EQ("init rc", logfake_sys_init_woz_logfmt_init(), 0);
	T_EQ("freq fallback 64MHz", (long)logfake.ts_freq, 64000000L);
	T_OK("DWT counter enabled", (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
	T_OK("trace unit enabled", (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0u);
	T_OK("timestamp fn registered", logfake.ts_fn != NULL);
	T_OK("formatter registered", logfake.formatter != NULL);
	/* every backend, active or not, including the -ENOTSUP last one */
	T_EQ("format set on all backends", logfake.format_set_calls, 3);
	T_EQ("format is CUSTOM", (long)logfake.last_format, LOG_OUTPUT_CUSTOM);
	T_OK("wrap timer started", logfake.timer != NULL);
	T_EQ("wrap timer period 16s", (long)logfake.timer_period_ms, 16000L);

	/* re-init with a live clock: freq re-registered */
	logfake.core_clock = 128000000;
	(void)logfake_sys_init_woz_logfmt_init();
	T_EQ("freq from SystemCoreClock", (long)logfake.ts_freq, 128000000L);

	/* --- timestamp: wrap-free 64-bit accumulation ------------------------ */
	t_group("timestamp");
	DWT->CYCCNT = 100; /* init left last=0 */
	T_OK("monotonic step", logfake.ts_fn() == 100u);
	DWT->CYCCNT = 50; /* u32 counter wrapped past zero */
	T_OK("counter wrap accumulated", logfake.ts_fn() == 4294967346ull); /* 2^32+50 */
	DWT->CYCCNT = 60;
	logfake.timer->expiry_fn(logfake.timer); /* idle sampler advances silently */
	T_OK("sampler keeps accumulator", logfake.ts_fn() == 4294967356ull);

	/* --- plain message ---------------------------------------------------- */
	t_group("format");
	logfake.source_name[2] = "uwb";
	/* 1.5 s at 128 MHz: sec + ns split re-derived by hand */
	struct log_msg m = mk(LOG_LEVEL_INF, 2, 192000000ull, "hello 7");

	fmt(&m, 0);
	T_OK("plain line exact", strcmp((char *)cap, "1.500000000 uwb hello 7\r\n") == 0);
	fmt(&m, LOG_OUTPUT_FLAG_COLORS);
	T_OK("colored ts dim prefix",
	     strncmp((char *)cap, "\x1b[2m1.500000000 \x1b[0m", 20) == 0);
	T_OK("colored tag present", strstr((char *)cap, "uwb\x1b[0m hello 7") != NULL);
	uint8_t first[1024];
	size_t first_len = cap_len;

	memcpy(first, cap, cap_len);
	fmt(&m, LOG_OUTPUT_FLAG_COLORS); /* stable colour: same name, same bytes */
	T_OK("module colour stable", cap_len == first_len && memcmp(cap, first, cap_len) == 0);

	m.level = LOG_LEVEL_ERR;
	fmt(&m, LOG_OUTPUT_FLAG_COLORS);
	T_OK("ERR forces red tag", strstr((char *)cap, "\x1b[31muwb") != NULL);
	m.level = LOG_LEVEL_WRN;
	fmt(&m, LOG_OUTPUT_FLAG_COLORS);
	T_OK("WRN forces yellow tag", strstr((char *)cap, "\x1b[33muwb") != NULL);

	/* --- boot banner (no source): "*** ... ***" wrapper stripped --------- */
	t_group("banner");
	m = mk(LOG_LEVEL_INF, -1, 0, "*** Booting Zephyr 3.3.0 ***\n");
	fmt(&m, 0);
	T_OK("banner stripped", strcmp((char *)cap, "0.000000000 Booting Zephyr 3.3.0\r\n") == 0);
	fmt(&m, LOG_OUTPUT_FLAG_COLORS);
	T_OK("banner bold-bright", strstr((char *)cap, "\x1b[1;97mBooting Zephyr 3.3.0\x1b[0m") != NULL);
	m = mk(LOG_LEVEL_INF, -1, 0, "*x*"); /* too short for the wrapper */
	fmt(&m, 0);
	T_OK("short body untouched", strcmp((char *)cap, "0.000000000 *x*\r\n") == 0);

	/* --- hexdump payload inlined ----------------------------------------- */
	t_group("hexdump");
	uint8_t payload[3] = {0xde, 0xad, 0xbe};

	m = mk(LOG_LEVEL_INF, 2, 0, "msg");
	m.data = payload;
	m.dlen = sizeof(payload);
	fmt(&m, 0);
	T_OK("hex inlined", strcmp((char *)cap, "0.000000000 uwb msg de ad be\r\n") == 0);

	/* --- packageless message: delegate to the stock renderer ------------- */
	t_group("delegate");
	m = mk(LOG_LEVEL_INF, 2, 0, "");
	m.data = payload;
	m.dlen = sizeof(payload);
	fmt(&m, 0);
	T_EQ("delegated to msg_process", logfake.msg_process_calls, 1);
	T_EQ("nothing self-rendered", (long)cap_len, 0L);

	/* --- 320-byte line cap: truncate, drop hex, never overrun ------------ */
	t_group("line cap");
	char big[401];

	memset(big, 'A', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	m = mk(LOG_LEVEL_INF, 2, 0, big);
	m.data = payload;
	m.dlen = sizeof(payload);
	fmt(&m, 0);
	T_EQ("line capped at 320", (long)cap_len, 320L);
	T_OK("cap keeps prefix", strncmp((char *)cap, "0.000000000 uwb AAAA", 20) == 0);
	T_OK("hex dropped when full", cap[cap_len - 1] == 'A');
}
