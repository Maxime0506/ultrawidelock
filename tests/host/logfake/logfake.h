/**
 * @file logfake.h — test-side control/inspection API for the fake Zephyr
 * logging surface under tests/host/logfake/zephyr/.
 *
 * Just enough of the logging + CMSIS API for woz_logfmt.c / woz_logquiet.c to
 * compile and run on host. Everything the code under test registers or calls
 * is recorded here for the suite to assert on; everything it reads (backend
 * count, source names, core clock, DWT counter) is a knob the suite sets.
 */
#ifndef WOZ_LOGFAKE_H
#define WOZ_LOGFAKE_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>

#define LOGFAKE_MAX_BACKENDS 4
#define LOGFAKE_MAX_SOURCES  8
#define LOGFAKE_MAX_FILTERS  8

struct logfake_state {
	/* recorded: registrations made by the code under test */
	log_timestamp_t (*ts_fn)(void);
	uint32_t ts_freq;
	void (*formatter)(const struct log_output *output, struct log_msg *msg, uint32_t flags);
	struct k_timer *timer; /* last k_timer_start target */
	int64_t timer_duration_ms;
	int64_t timer_period_ms;
	int format_set_calls;
	uint32_t last_format;
	int msg_process_calls; /* log_output_msg_process delegations */
	int filter_set_calls;  /* woz_logquiet mutes */
	int16_t filter_sid[LOGFAKE_MAX_FILTERS];
	uint32_t filter_level[LOGFAKE_MAX_FILTERS];

	/* knobs: environment the code under test observes */
	uint32_t core_clock; /* copied to SystemCoreClock by SystemCoreClockUpdate() */
	int backend_count;
	const char *source_name[LOGFAKE_MAX_SOURCES]; /* index == source id */
};

extern struct logfake_state logfake;

/** @brief Zero all recordings and knobs (backend_count included). */
void logfake_reset(void);

#endif /* WOZ_LOGFAKE_H */
