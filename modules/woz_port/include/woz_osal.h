/*
 * woz_osal.h - deferred work, semaphores and threads for the shared modules.
 *
 * The second half of the platform contract (woz_port.h is the first): exactly
 * the primitives the converted call sites use, nothing speculative. Work and
 * delayable work carry k_work_delayable semantics VERBATIM -- schedule is a
 * no-op while pending (the earlier deadline stands), reschedule restarts the
 * delay -- because uwb_rxdiag's windowing depends on that distinction. There
 * is deliberately no timer API: every periodic need at a real call site is
 * delayable work.
 *
 * Context rules, unenforced: woz_work_submit and woz_sem_give are ISR-safe on
 * Zephyr only. On ESP-IDF call them from task context -- the DW3000 port
 * already defers its IRQ to a task (ports/esp32/.../dw3000_hw.c), and every
 * converted module runs behind that deferral.
 *
 * Backends: Zephyr maps 1:1 onto k_work/k_sem/k_thread (osal_zephyr.c holds
 * the handler trampolines). ESP-IDF runs one dispatch task over a FreeRTOS
 * queue plus a deadline list (osal_esp.c). The host backend IS the test
 * double: a FIFO and a virtual clock the suite steps by hand (osal_host.c),
 * so converted modules keep deterministic host tests without a faked
 * <zephyr/kernel.h>.
 */
#ifndef WOZ_OSAL_H
#define WOZ_OSAL_H

#include <stddef.h>
#include <stdint.h>

struct woz_work;
struct woz_dwork;
typedef void (*woz_work_fn)(struct woz_work *work);
typedef void (*woz_dwork_fn)(struct woz_dwork *dwork);

#define WOZ_WAIT_FOREVER (-1)

enum woz_thread_prio {
	WOZ_THREAD_PRIO_LOW,
	WOZ_THREAD_PRIO_NORM, /* the NFC transport thread's level */
	WOZ_THREAD_PRIO_HIGH,
};

#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

struct woz_work {
	struct k_work kw;
	woz_work_fn fn;
};
struct woz_dwork {
	struct k_work_delayable kw;
	woz_dwork_fn fn;
};
typedef struct k_sem woz_sem_t;
typedef struct {
	struct k_thread thread;
} woz_thread_t;
typedef k_thread_stack_t woz_thread_stack_t;
#define WOZ_THREAD_STACK_DEFINE(name, size) K_THREAD_STACK_DEFINE(name, size)
#define WOZ_THREAD_STACK_SIZEOF(name)       K_THREAD_STACK_SIZEOF(name)

/* Application-level init hook; the fn is int (*)(void), 0 on success. */
#define WOZ_INIT_APPLICATION(fn) SYS_INIT(fn, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)
static inline int woz_osal_init_all(void)
{
	return 0; /* SYS_INIT already ran everything */
}

#elif defined(ESP_PLATFORM)

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct woz_work {
	woz_work_fn fn;
	struct woz_work *next;
	volatile int pending;
};
struct woz_dwork {
	woz_dwork_fn fn;
	struct woz_dwork *next;
	int64_t deadline_us;
	volatile int pending;
};
typedef struct {
	StaticSemaphore_t buf;
	SemaphoreHandle_t h;
} woz_sem_t;
typedef struct {
	StaticTask_t tcb;
	TaskHandle_t handle;
} woz_thread_t;
typedef StackType_t woz_thread_stack_t;
#define WOZ_THREAD_STACK_DEFINE(name, size) static StackType_t name[(size) / sizeof(StackType_t)]
#define WOZ_THREAD_STACK_SIZEOF(name)       sizeof(name)

#define WOZ_INIT_APPLICATION(fn)                                     \
	__attribute__((constructor)) static void woz_reg_##fn(void) \
	{                                                            \
		woz_osal_init_register(fn);                          \
	}
void woz_osal_init_register(int (*fn)(void));
int woz_osal_init_all(void); /* app_main calls this once, after the OS is up */

#elif defined(WOZ_PORT_HOST)

struct woz_work {
	woz_work_fn fn;
	struct woz_work *next;
	int pending;
};
struct woz_dwork {
	woz_dwork_fn fn;
	struct woz_dwork *next;
	int64_t deadline_ms;
	int pending;
};
typedef struct {
	unsigned count;
	unsigned limit;
} woz_sem_t;
/* Host threads never run: create() records entry and arg, and the suite
 * drives the entry function itself, exactly like the other recording fakes. */
typedef struct {
	void (*entry)(void *arg);
	void *arg;
	const char *name;
	int created;
} woz_thread_t;
typedef uint8_t woz_thread_stack_t;
#define WOZ_THREAD_STACK_DEFINE(name, size) static uint8_t name[(size)]
#define WOZ_THREAD_STACK_SIZEOF(name)       sizeof(name)

/* Registers for woz_osal_init_all() AND exposes the (usually static) init
 * function as a linkable pointer, so a suite can invoke one hook by itself at
 * a controlled point -- the same reach logfake's SYS_INIT fake used to give. */
#define WOZ_INIT_APPLICATION(fn)                                     \
	int (*const woz_init_##fn)(void) = (fn);                    \
	__attribute__((constructor)) static void woz_reg_##fn(void) \
	{                                                            \
		woz_osal_init_register(fn);                          \
	}
void woz_osal_init_register(int (*fn)(void));
int woz_osal_init_all(void);

/* ---- suite controls (host backend only) ---------------------------------
 *
 * The virtual clock starts at 0 and moves only when a suite advances it; it
 * is deliberately independent of woz_uptime_ms (real time), so a suite that
 * needs both aligned drives both.
 */

/** @brief Drop all queued work, disarm everything, clock back to 0. */
void woz_osal_host_reset(void);

/** @brief Run the immediate work queued right now (not what those handlers
 * queue -- a self-resubmitting handler is stepped one flush at a time).
 * @return how many handlers ran. */
unsigned woz_osal_host_flush(void);

/** @brief Advance the virtual clock, running due delayable work in deadline
 * order. Immediate work stays queued for woz_osal_host_flush().
 * @return how many handlers ran. */
unsigned woz_osal_host_advance_ms(int64_t ms);

/** @brief The virtual clock. */
int64_t woz_osal_host_now_ms(void);

#else
#error "woz_osal.h: no platform backend. Define WOZ_PORT_HOST, or build under Zephyr/ESP-IDF."
#endif

/* ---- the contract, identical on every backend --------------------------- */

void woz_work_init(struct woz_work *work, woz_work_fn fn);
/** @brief Queue @p work; a no-op while it is already queued. ISR-safe on
 * Zephyr only. @return 0 queued or already pending, negative on error. */
int woz_work_submit(struct woz_work *work);

void woz_dwork_init(struct woz_dwork *dwork, woz_dwork_fn fn);
/** @brief Arm @p dwork for @p delay_ms from now; a NO-OP while armed (the
 * earlier deadline stands). 0 means as soon as possible. */
int woz_dwork_schedule(struct woz_dwork *dwork, int32_t delay_ms);
/** @brief Arm @p dwork for @p delay_ms from now, RESTARTING the delay if it
 * was already armed. */
int woz_dwork_reschedule(struct woz_dwork *dwork, int32_t delay_ms);
/** @brief Disarm @p dwork if armed; a completed or idle one is left alone. */
int woz_dwork_cancel(struct woz_dwork *dwork);

/** @brief Counting semaphore. @p limit caps the count (every call site today
 * uses limit 1: a binary "data ready" flag). */
void woz_sem_init(woz_sem_t *sem, unsigned initial, unsigned limit);
/** @brief Give; saturates at the limit. ISR-safe on Zephyr only. */
void woz_sem_give(woz_sem_t *sem);
/** @brief Take within @p timeout_ms (WOZ_WAIT_FOREVER blocks). On the host
 * backend a timed take advances the virtual clock, running delayable work
 * that falls due -- which is how a "give" arrives in a single-threaded test.
 * @return 0 taken, -1 timed out. */
int woz_sem_take(woz_sem_t *sem, int32_t timeout_ms);
/** @brief Count back to zero; waiters (Zephyr) fail with -1. */
void woz_sem_reset(woz_sem_t *sem);

/** @brief Start a static-stacked thread. @p stack_size is
 * WOZ_THREAD_STACK_SIZEOF(the WOZ_THREAD_STACK_DEFINE'd array).
 * @return 0 on success. */
int woz_thread_create(woz_thread_t *thread, woz_thread_stack_t *stack, size_t stack_size,
		      void (*entry)(void *arg), void *arg, enum woz_thread_prio prio,
		      const char *name);

#endif /* WOZ_OSAL_H */
