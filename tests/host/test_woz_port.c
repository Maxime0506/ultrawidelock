/**
 * @file test_woz_port.c — the OSAL and flash contracts, on the host backend.
 *
 * Files under test: modules/woz_port/src/osal_host.c and flash_host.c — the
 * backend every converted module's host suite runs on, so its semantics ARE
 * the portability contract: schedule is a no-op while pending, reschedule
 * restarts the delay, a timed sem take walks the virtual clock, flash refuses
 * unaligned writes. A drift here would let module tests pass against
 * behaviour no target exhibits.
 */
#include <string.h>

#include "test.h"

#include "woz_flash.h"
#include "woz_osal.h"

/* ---- recorders -------------------------------------------------------------- */

static int g_runs[8];
static int g_order[8];
static int g_order_n;

static void run_count(struct woz_work *w)
{
	(void)w;
	g_runs[0]++;
}

static struct woz_work g_self;
static int g_self_runs;
static void run_resubmit(struct woz_work *w)
{
	g_self_runs++;
	if (g_self_runs == 1) {
		woz_work_submit(w);
	}
}

static void dwork_a(struct woz_dwork *w)
{
	(void)w;
	g_runs[1]++;
	g_order[g_order_n++] = 1;
}
static void dwork_b(struct woz_dwork *w)
{
	(void)w;
	g_runs[2]++;
	g_order[g_order_n++] = 2;
}

static struct woz_dwork g_rearm;
static int g_rearm_runs;
static void dwork_rearm(struct woz_dwork *w)
{
	g_rearm_runs++;
	if (g_rearm_runs == 1) {
		woz_dwork_schedule(w, 10);
	}
}

static woz_sem_t g_sem;
static void dwork_give(struct woz_dwork *w)
{
	(void)w;
	woz_sem_give(&g_sem);
}

static void thread_entry(void *arg)
{
	*(int *)arg = 1;
}

static int g_hook_runs;
static int hook_fn(void)
{
	g_hook_runs++;
	return 0;
}
WOZ_INIT_APPLICATION(hook_fn);

/* ---- the suite --------------------------------------------------------------- */

static void test_work(void)
{
	struct woz_work w;

	t_group("osal: immediate work");
	woz_osal_host_reset();
	memset(g_runs, 0, sizeof(g_runs));

	woz_work_init(&w, run_count);
	T_EQ("nothing queued runs nothing", (long)woz_osal_host_flush(), 0L);
	woz_work_submit(&w);
	T_EQ("submit twice while pending queues once", woz_work_submit(&w), 0L);
	T_EQ("flush runs it once", (long)woz_osal_host_flush(), 1L);
	T_EQ("the handler ran once", (long)g_runs[0], 1L);
	T_EQ("the queue is drained", (long)woz_osal_host_flush(), 0L);

	g_self_runs = 0;
	woz_work_init(&g_self, run_resubmit);
	woz_work_submit(&g_self);
	T_EQ("a self-resubmitting handler runs once per flush", (long)woz_osal_host_flush(), 1L);
	T_EQ("its resubmission waits for the next flush", (long)woz_osal_host_flush(), 1L);
	T_EQ("then the queue is quiet", (long)woz_osal_host_flush(), 0L);
	T_EQ("two runs total", (long)g_self_runs, 2L);
}

static void test_dwork(void)
{
	struct woz_dwork a, b;

	t_group("osal: delayable work");
	woz_osal_host_reset();
	memset(g_runs, 0, sizeof(g_runs));
	g_order_n = 0;

	woz_dwork_init(&a, dwork_a);
	woz_dwork_init(&b, dwork_b);

	/* The k_work_delayable distinction the rxdiag windowing depends on:
	 * schedule keeps the earlier deadline, reschedule restarts it. */
	woz_dwork_schedule(&a, 50);
	woz_osal_host_advance_ms(10);
	woz_dwork_schedule(&a, 100);
	T_EQ("schedule while armed is a no-op", (long)woz_osal_host_advance_ms(39), 0L);
	T_EQ("the original deadline stands", (long)woz_osal_host_advance_ms(1), 1L);
	T_EQ("ran exactly once", (long)g_runs[1], 1L);

	woz_dwork_schedule(&a, 50);
	woz_osal_host_advance_ms(10);
	woz_dwork_reschedule(&a, 50);
	T_EQ("reschedule restarted the delay", (long)woz_osal_host_advance_ms(49), 0L);
	T_EQ("new deadline fires", (long)woz_osal_host_advance_ms(1), 1L);

	woz_dwork_reschedule(&a, 0);
	T_EQ("delay 0 is due immediately", (long)woz_osal_host_advance_ms(0), 1L);

	woz_dwork_schedule(&a, 30);
	woz_dwork_cancel(&a);
	T_EQ("cancelled work never fires", (long)woz_osal_host_advance_ms(100), 0L);
	T_EQ("cancel of idle work is harmless", woz_dwork_cancel(&a), 0L);

	g_order_n = 0;
	woz_dwork_schedule(&b, 20);
	woz_dwork_schedule(&a, 10);
	T_EQ("both fire in one window", (long)woz_osal_host_advance_ms(25), 2L);
	T_OK("deadline order, not submit order", g_order[0] == 1 && g_order[1] == 2);

	g_rearm_runs = 0;
	woz_dwork_init(&g_rearm, dwork_rearm);
	woz_dwork_schedule(&g_rearm, 5);
	T_EQ("a re-arming handler fires again next window", (long)woz_osal_host_advance_ms(15), 2L);
	T_EQ("re-armed twice total", (long)g_rearm_runs, 2L);
}

static void test_sem(void)
{
	struct woz_dwork give;

	t_group("osal: semaphore");
	woz_osal_host_reset();

	woz_sem_init(&g_sem, 0, 1);
	T_EQ("empty take with no wait fails", woz_sem_take(&g_sem, 0), -1L);
	woz_sem_give(&g_sem);
	T_EQ("give then take succeeds", woz_sem_take(&g_sem, 0), 0L);
	woz_sem_give(&g_sem);
	woz_sem_give(&g_sem);
	T_EQ("gives saturate at the limit", woz_sem_take(&g_sem, 0), 0L);
	T_EQ("only one was kept", woz_sem_take(&g_sem, 0), -1L);
	woz_sem_give(&g_sem);
	woz_sem_reset(&g_sem);
	T_EQ("reset drops the count", woz_sem_take(&g_sem, 0), -1L);

	/* The single-thread contract: a timed take advances the clock and a
	 * delayable give lands inside the window. */
	woz_dwork_init(&give, dwork_give);
	woz_dwork_schedule(&give, 30);
	T_EQ("a give due at 30ms satisfies a 100ms take", woz_sem_take(&g_sem, 100), 0L);
	T_EQ("the take consumed the full window", (long)woz_osal_host_now_ms(), 100L);

	woz_dwork_schedule(&give, 50);
	T_EQ("a give due past the window times out", woz_sem_take(&g_sem, 20), -1L);
	T_EQ("the timeout still elapsed", (long)woz_osal_host_now_ms(), 120L);
	T_EQ("the late give then satisfies a forever take", woz_sem_take(&g_sem, WOZ_WAIT_FOREVER), 0L);
}

static void test_thread_and_init(void)
{
	static WOZ_THREAD_STACK_DEFINE(stack, 512);
	woz_thread_t t;
	int flag = 0;

	t_group("osal: thread + init hooks");
	T_EQ("create records and reports success",
	     woz_thread_create(&t, stack, WOZ_THREAD_STACK_SIZEOF(stack), thread_entry, &flag,
			       WOZ_THREAD_PRIO_NORM, "t"), 0L);
	T_OK("host threads do not run by themselves", flag == 0 && t.created == 1);
	t.entry(t.arg);
	T_EQ("the suite drives the entry", (long)flag, 1L);

	T_EQ("init hooks run on demand", woz_osal_init_all(), 0L);
	T_EQ("the registered hook ran", (long)g_hook_runs, 1L);
	woz_osal_init_all();
	T_EQ("init_all is idempotent", (long)g_hook_runs, 1L);
}

static void test_flash(void)
{
	const struct woz_flash_area *fa;
	struct woz_flash_host_area *host;
	static const uint8_t word[4] = { 0xde, 0xad, 0xca, 0xfe };
	uint8_t back[4];

	t_group("flash: host partitions");
	woz_flash_host_reset();

	T_EQ("staging opens", woz_flash_open(WOZ_FLASH_AREA_STAGING, &fa), 0L);
	T_EQ("staging geometry matches pm_static.yml",
	     (long)woz_flash_size(fa), (long)WOZ_FLASH_HOST_STAGING_SIZE);
	host = woz_flash_host_area(WOZ_FLASH_AREA_STAGING);

	T_EQ("aligned write lands", woz_flash_write(fa, 8, word, sizeof(word)), 0L);
	T_EQ("the bytes are there", (long)host->buf[8], 0xdeL);
	T_EQ("read round-trips", woz_flash_read(fa, 8, back, sizeof(back)), 0L);
	T_OK("read matches write", memcmp(back, word, sizeof(word)) == 0);

	T_EQ("unaligned write offset refused", woz_flash_write(fa, 2, word, 4), -1L);
	T_EQ("unaligned write length refused", woz_flash_write(fa, 0, word, 3), -1L);
	T_EQ("unaligned erase refused", woz_flash_erase(fa, 100, 4096), -1L);
	T_EQ("erase off the end refused",
	     woz_flash_erase(fa, 0, WOZ_FLASH_HOST_STAGING_SIZE + 4096), -1L);
	T_EQ("write off the end refused",
	     woz_flash_write(fa, WOZ_FLASH_HOST_STAGING_SIZE - 2, word, 4), -1L);

	T_EQ("page erase succeeds", woz_flash_erase(fa, 0, 4096), 0L);
	T_EQ("erase restored 0xff", (long)host->buf[8], 0xffL);

	host->write_fail_in = 1;
	T_EQ("fail_in lets N calls through", woz_flash_write(fa, 0, word, 4), 0L);
	T_EQ("then fails every call", woz_flash_write(fa, 0, word, 4), -1L);
	T_EQ("still failing", woz_flash_write(fa, 0, word, 4), -1L);

	woz_flash_close(fa);
	T_EQ("calls were recorded", (long)host->close_calls, 1L);

	host->fail_open = 1;
	T_EQ("fail_open refuses", woz_flash_open(WOZ_FLASH_AREA_STAGING, &fa), -1L);

	T_EQ("no reboot yet", (long)woz_flash_host_reboots(), 0L);
	woz_reboot();
	T_EQ("reboot recorded, not taken", (long)woz_flash_host_reboots(), 1L);

	woz_flash_host_reset();
	host = woz_flash_host_area(WOZ_FLASH_AREA_STAGING);
	T_OK("reset restores knobs and recorders",
	     host->fail_open == 0 && host->write_fail_in == -1 && host->write_calls == 0);
	T_EQ("primary opens after reset", woz_flash_open(WOZ_FLASH_AREA_PRIMARY, &fa), 0L);
	T_EQ("primary geometry matches pm_static.yml",
	     (long)woz_flash_size(fa), (long)WOZ_FLASH_HOST_PRIMARY_SIZE);
}

void test_woz_port(void)
{
	test_work();
	test_dwork();
	test_sem();
	test_thread_and_init();
	test_flash();
}
