/** @file logfake.c — shared state + non-inline definitions for the fake
 * Zephyr logging surface (see logfake.h). */
#include "logfake.h"

#include <string.h>

#include <cmsis_core.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/sys/cbprintf.h>

struct logfake_state logfake;

/* --- CMSIS fakes ------------------------------------------------------------ */
static logfake_dwt_t dwt_regs;
static logfake_coredebug_t coredebug_regs;
logfake_dwt_t *DWT = &dwt_regs;
logfake_coredebug_t *CoreDebug = &coredebug_regs;

uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void)
{
	SystemCoreClock = logfake.core_clock;
}

void logfake_reset(void)
{
	memset(&logfake, 0, sizeof(logfake));
	memset(&dwt_regs, 0, sizeof(dwt_regs));
	memset(&coredebug_regs, 0, sizeof(coredebug_regs));
	SystemCoreClock = 0u;
}

/* --- kernel ----------------------------------------------------------------- */
void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period)
{
	logfake.timer = timer;
	logfake.timer_duration_ms = duration;
	logfake.timer_period_ms = period;
}

/* --- cbprintf: a package is the already-rendered string --------------------- */
int cbpprintf(cbprintf_cb out, void *ctx, void *packaged)
{
	const char *s = packaged;
	int n = 0;

	while (*s != '\0') {
		out((int)(unsigned char)*s++, ctx);
		n++;
	}
	return n;
}

/* --- logging registrations + queries ----------------------------------------- */
int log_set_timestamp_func(log_timestamp_get_t fn, uint32_t freq)
{
	logfake.ts_fn = fn;
	logfake.ts_freq = freq;
	return 0;
}

void log_custom_output_msg_set(void (*fn)(const struct log_output *output,
					  struct log_msg *msg, uint32_t flags))
{
	logfake.formatter = fn;
}

static struct log_backend backends[LOGFAKE_MAX_BACKENDS];

int log_backend_count_get(void)
{
	return logfake.backend_count;
}

const struct log_backend *log_backend_get(int idx)
{
	backends[idx].id = idx;
	return &backends[idx];
}

int log_backend_format_set(const struct log_backend *backend, uint32_t format)
{
	logfake.format_set_calls++;
	logfake.last_format = format;
	/* Mirror the real API's mixed support: the last backend rejects the
	 * format, which the caller must tolerate (-ENOTSUP is documented). */
	return (backend->id == logfake.backend_count - 1) ? -134 : 0;
}

const char *log_source_name_get(uint32_t domain_id, int16_t source_id)
{
	(void)domain_id;
	if (source_id < 0 || source_id >= LOGFAKE_MAX_SOURCES) {
		return NULL;
	}
	return logfake.source_name[source_id];
}

int16_t log_source_id_get(const char *name)
{
	for (int16_t i = 0; i < LOGFAKE_MAX_SOURCES; i++) {
		if (logfake.source_name[i] != NULL &&
		    strcmp(logfake.source_name[i], name) == 0) {
			return i;
		}
	}
	return -1;
}

uint32_t log_filter_set(struct log_backend *backend, uint32_t domain_id,
			int16_t source_id, uint32_t level)
{
	(void)backend;
	(void)domain_id;
	if (logfake.filter_set_calls < LOGFAKE_MAX_FILTERS) {
		logfake.filter_sid[logfake.filter_set_calls] = source_id;
		logfake.filter_level[logfake.filter_set_calls] = level;
	}
	logfake.filter_set_calls++;
	return level;
}

/* --- output ----------------------------------------------------------------- */
void log_output_msg_process(const struct log_output *output, struct log_msg *msg,
			    uint32_t flags)
{
	(void)output;
	(void)msg;
	(void)flags;
	logfake.msg_process_calls++;
}

void log_output_write(log_output_func_t func, uint8_t *buf, size_t len, void *ctx)
{
	func(buf, len, ctx);
}
