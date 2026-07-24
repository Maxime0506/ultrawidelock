/* logfake: minimal <zephyr/kernel.h> for host-building the log facade. */
#ifndef LOGFAKE_ZEPHYR_KERNEL_H
#define LOGFAKE_ZEPHYR_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

/* snprintk is snprintf with a trimmed format set; host snprintf covers it. */
#define snprintk snprintf

static inline unsigned int irq_lock(void)
{
	return 0u;
}
static inline void irq_unlock(unsigned int key)
{
	(void)key;
}

struct k_timer;
typedef void (*k_timer_expiry_t)(struct k_timer *timer);
struct k_timer {
	k_timer_expiry_t expiry_fn;
	void (*stop_fn)(struct k_timer *timer);
};
#define K_TIMER_DEFINE(name, expiry, stop) struct k_timer name = {(expiry), (stop)}

typedef int64_t k_timeout_t; /* milliseconds in this fake */
#define K_SECONDS(s) ((k_timeout_t)(s) * 1000)
#define K_MSEC(ms)   ((k_timeout_t)(ms))
#define K_NO_WAIT    ((k_timeout_t)0)

void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period);

/* ── k_work surface for the driver-binary suites (uwb_rxdiag / uwb_selftest).
 * The recording implementations live in tests/host/shim/drvfake.c, which only
 * the driver test binary links; the main host binary never references them. */
#include <errno.h>

#ifndef printk
#define printk printf
#endif

#ifndef IS_ENABLED
/* Zephyr's config-macro probe: 1 iff the macro is defined to 1, else 0. */
#define LOGFAKE_IE3(ignore_this, val, ...) val
/* The token-paste probe cannot parenthesize its argument. */
/* NOLINTNEXTLINE(bugprone-macro-parentheses) */
#define LOGFAKE_IE2(one_or_two_args)       LOGFAKE_IE3(one_or_two_args 1, 0)
#define _XXXX1                             LOGFAKE_YYYY,
#define LOGFAKE_IE1(config_macro)          LOGFAKE_IE2(_XXXX##config_macro)
#define IS_ENABLED(config_macro)           LOGFAKE_IE1(config_macro)
#endif

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);
struct k_work {
	k_work_handler_t handler;
};
struct k_work_delayable {
	struct k_work work;
};
#define K_WORK_DELAYABLE_DEFINE(name, fn) struct k_work_delayable name = {{(fn)}}

int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
int k_work_cancel_delayable(struct k_work_delayable *dwork);
void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler);

/* Recorded by the fakes above; the suite fires a work item by calling
 * workfake.last->work.handler(&workfake.last->work) itself. */
struct workfake_state {
	struct k_work_delayable *last;
	k_timeout_t last_delay;
	unsigned int reschedule_calls;
	unsigned int schedule_calls;
	unsigned int cancel_calls;
};
extern struct workfake_state workfake;

#endif /* LOGFAKE_ZEPHYR_KERNEL_H */
