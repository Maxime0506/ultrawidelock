/*
 * The kernel services the shared Matter sources use, over this port's OSAL.
 *
 * Two files are carried across on this header: the Thread transport, which uses
 * only k_msleep, and the commissioning layer, which uses work items. Neither
 * uses anything else from zephyr/kernel.h, and that is asserted rather than
 * assumed -- scripts/freertos-matter-source-check.sh fails the build if either
 * starts naming something this file does not provide.
 */
#ifndef WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H
#define WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H

/* Zephyr's kernel.h drags these in, and the shared sources rely on that:
 * errno for the EINVAL they return, stddef for size_t in their prototypes. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_osal.h>

#define ARG_UNUSED(x) ((void)(x))

/*
 * Zephyr's message argument is optional and C11's _Static_assert requires one.
 * Concatenating with an empty literal covers both: with no message the result is
 * _Static_assert(expr, ""), and with one it is string concatenation, which is
 * done by the compiler rather than by an argument-counting macro.
 */
#define BUILD_ASSERT(expr, ...) _Static_assert(expr, "" __VA_ARGS__)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/*
 * Zephyr's config-symbol test. A symbol that is not defined at all evaluates to
 * 0 in #if, which is the behaviour these call sites want.
 */
#ifndef IS_ENABLED
#define IS_ENABLED(cfg) (cfg)
#endif

/**
 * k_msleep, used once by the Thread transport while polling the role. Runs on a
 * task, never in an interrupt, so a plain delay is the whole translation.
 */
static inline void k_msleep(uint32_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

/*
 * TIMEOUTS ARE PLAIN MILLISECONDS HERE.
 *
 * Zephyr's k_timeout_t is an opaque struct in tick units; this port's OSAL takes
 * int32_t milliseconds. Every call site in the shared sources passes one of the
 * three macros below, so the representation never escapes this header.
 */
typedef int32_t k_timeout_t;
#define K_NO_WAIT     ((k_timeout_t)0)
#define K_MSEC(ms)    ((k_timeout_t)(ms))
#define K_SECONDS(s)  ((k_timeout_t)(s) * 1000)

/*
 * WHY THESE WRAP RATHER THAN CAST.
 *
 * The shared handlers are declared void f(struct k_work *), and the OSAL's are
 * void f(struct woz_work *). Casting between the two function-pointer types
 * would work on this target and is undefined behaviour, which is a poor trade
 * for a header that already has to be read carefully. Instead the OSAL calls a
 * trampoline of its own type, which recovers the k_work from the address of its
 * first member -- guaranteed by C to be the same address -- and calls the real
 * handler through a stored pointer.
 *
 * The woz_work member MUST stay first for that to hold.
 */
struct k_work {
	struct woz_work w;
	void (*handler)(struct k_work *work);
};

struct k_work_delayable {
	struct woz_dwork d;
	void (*handler)(struct k_work *work);
};

static inline void woz_k_work_tramp(struct woz_work *w)
{
	struct k_work *kw = (struct k_work *)(void *)w;

	kw->handler(kw);
}

static inline void woz_k_dwork_tramp(struct woz_dwork *d)
{
	struct k_work_delayable *kd = (struct k_work_delayable *)(void *)d;

	/*
	 * Zephyr hands a delayable handler the address of the INNER k_work, and
	 * the shared sources declare these handlers as taking struct k_work *.
	 * None of them dereferences it -- each reaches its own file-scope state
	 * instead -- so passing the containing object's address is safe here and
	 * keeps one handler signature. If a future handler ever uses the
	 * pointer, this is the line that has to change.
	 */
	kd->handler((struct k_work *)(void *)kd);
}

#define K_WORK_DEFINE(sym, handler_fn)                                                             \
	struct k_work sym = {                                                                      \
		.w = { .fn = woz_k_work_tramp, .next = NULL, .pending = 0 },                       \
		.handler = (handler_fn),                                                           \
	}

#define K_WORK_DELAYABLE_DEFINE(sym, handler_fn)                                                   \
	struct k_work_delayable sym = {                                                            \
		.d = { .fn = woz_k_dwork_tramp, .next = NULL, .deadline_us = 0, .pending = 0 },    \
		.handler = (handler_fn),                                                           \
	}

static inline int k_work_submit(struct k_work *work)
{
	return woz_work_submit(&work->w);
}

static inline int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return woz_dwork_schedule(&dwork->d, delay);
}

static inline int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return woz_dwork_reschedule(&dwork->d, delay);
}

static inline int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	return woz_dwork_cancel(&dwork->d);
}

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H */
