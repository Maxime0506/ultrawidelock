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

/* No k_work surface here, deliberately. Deferred work in shared code goes
 * through woz_osal.h, whose host backend (modules/woz_port/src/osal_host.c) IS
 * the fake — one work queue and one virtual clock for every host binary,
 * instead of a recording double per fake tree. A file that still needs
 * <zephyr/kernel.h>'s work API is port glue and belongs under ports/. */

#endif /* LOGFAKE_ZEPHYR_KERNEL_H */
