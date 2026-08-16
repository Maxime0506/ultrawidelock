/**
 * @file test_ultrawidelock_port.c — the OSAL and flash contracts, on the host backend.
 *
 * Files under test: tests/host/port/osal_host.c and flash_host.c — the
 * backend every converted module's host suite runs on, so its semantics ARE
 * the portability contract: schedule is a no-op while pending, reschedule
 * restarts the delay, a timed sem take walks the virtual clock, flash refuses
 * unaligned writes. A drift here would let module tests pass against
 * behaviour no target exhibits.
 */
#include <string.h>

#include "test.h"

#include "ultrawidelock_flash.h"
#include "ultrawidelock_osal.h"

/* ---- recorders -------------------------------------------------------------- */

static int g_runs[8];
static int g_order[8];
static int g_order_n;

static void run_count(struct ultrawidelock_work *w)
{
	(void)w;
	g_runs[0]++;
}

static struct ultrawidelock_work g_self;
static int g_self_runs;
static void run_resubmit(struct ultrawidelock_work *w)
{
	g_self_runs++;
	if (g_self_runs == 1) {
		ultrawidelock_work_submit(w);
	}
}

static void dwork_a(struct ultrawidelock_dwork *w)
{
	(void)w;
	g_runs[1]++;
	g_order[g_order_n++] = 1;
}
static void dwork_b(struct ultrawidelock_dwork *w)
{
	(void)w;
	g_runs[2]++;
	g_order[g_order_n++] = 2;
}

static struct ultrawidelock_dwork g_rearm;
static int g_rearm_runs;
static void dwork_rearm(struct ultrawidelock_dwork *w)
{
	g_rearm_runs++;
	if (g_rearm_runs == 1) {
		ultrawidelock_dwork_schedule(w, 10);
	}
}

static ultrawidelock_sem_t g_sem;
static void dwork_give(struct ultrawidelock_dwork *w)
{
	(void)w;
	ultrawidelock_sem_give(&g_sem);
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
ULTRAWIDELOCK_INIT_APPLICATION(hook_fn);

/* ---- the suite --------------------------------------------------------------- */

static void test_work(void)
{
	struct ultrawidelock_work w;

	t_group("osal: immediate work");
	ultrawidelock_osal_host_reset();
	memset(g_runs, 0, sizeof(g_runs));

	ultrawidelock_work_init(&w, run_count);
	T_EQ("nothing queued runs nothing", (long)ultrawidelock_osal_host_flush(), 0L);
	ultrawidelock_work_submit(&w);
	T_EQ("submit twice while pending queues once", ultrawidelock_work_submit(&w), 0L);
	T_EQ("flush runs it once", (long)ultrawidelock_osal_host_flush(), 1L);
	T_EQ("the handler ran once", (long)g_runs[0], 1L);
	T_EQ("the queue is drained", (long)ultrawidelock_osal_host_flush(), 0L);

	g_self_runs = 0;
	ultrawidelock_work_init(&g_self, run_resubmit);
	ultrawidelock_work_submit(&g_self);
	T_EQ("a self-resubmitting handler runs once per flush", (long)ultrawidelock_osal_host_flush(), 1L);
	T_EQ("its resubmission waits for the next flush", (long)ultrawidelock_osal_host_flush(), 1L);
	T_EQ("then the queue is quiet", (long)ultrawidelock_osal_host_flush(), 0L);
	T_EQ("two runs total", (long)g_self_runs, 2L);
}

static void test_dwork(void)
{
	struct ultrawidelock_dwork a, b;

	t_group("osal: delayable work");
	ultrawidelock_osal_host_reset();
	memset(g_runs, 0, sizeof(g_runs));
	g_order_n = 0;

	ultrawidelock_dwork_init(&a, dwork_a);
	ultrawidelock_dwork_init(&b, dwork_b);

	/* The k_work_delayable distinction the rxdiag windowing depends on:
	 * schedule keeps the earlier deadline, reschedule restarts it. */
	ultrawidelock_dwork_schedule(&a, 50);
	ultrawidelock_osal_host_advance_ms(10);
	ultrawidelock_dwork_schedule(&a, 100);
	T_EQ("schedule while armed is a no-op", (long)ultrawidelock_osal_host_advance_ms(39), 0L);
	T_EQ("the original deadline stands", (long)ultrawidelock_osal_host_advance_ms(1), 1L);
	T_EQ("ran exactly once", (long)g_runs[1], 1L);

	ultrawidelock_dwork_schedule(&a, 50);
	ultrawidelock_osal_host_advance_ms(10);
	ultrawidelock_dwork_reschedule(&a, 50);
	T_EQ("reschedule restarted the delay", (long)ultrawidelock_osal_host_advance_ms(49), 0L);
	T_EQ("new deadline fires", (long)ultrawidelock_osal_host_advance_ms(1), 1L);

	ultrawidelock_dwork_reschedule(&a, 0);
	T_EQ("delay 0 is due immediately", (long)ultrawidelock_osal_host_advance_ms(0), 1L);

	ultrawidelock_dwork_schedule(&a, 30);
	ultrawidelock_dwork_cancel(&a);
	T_EQ("cancelled work never fires", (long)ultrawidelock_osal_host_advance_ms(100), 0L);
	T_EQ("cancel of idle work is harmless", ultrawidelock_dwork_cancel(&a), 0L);

	g_order_n = 0;
	ultrawidelock_dwork_schedule(&b, 20);
	ultrawidelock_dwork_schedule(&a, 10);
	T_EQ("both fire in one window", (long)ultrawidelock_osal_host_advance_ms(25), 2L);
	T_OK("deadline order, not submit order", g_order[0] == 1 && g_order[1] == 2);

	g_rearm_runs = 0;
	ultrawidelock_dwork_init(&g_rearm, dwork_rearm);
	ultrawidelock_dwork_schedule(&g_rearm, 5);
	T_EQ("a re-arming handler fires again next window",
	     (long)ultrawidelock_osal_host_advance_ms(15), 2L);
	T_EQ("re-armed twice total", (long)g_rearm_runs, 2L);
}

static void test_sem(void)
{
	struct ultrawidelock_dwork give;

	t_group("osal: semaphore");
	ultrawidelock_osal_host_reset();

	ultrawidelock_sem_init(&g_sem, 0, 1);
	T_EQ("empty take with no wait fails", ultrawidelock_sem_take(&g_sem, 0), -1L);
	ultrawidelock_sem_give(&g_sem);
	T_EQ("give then take succeeds", ultrawidelock_sem_take(&g_sem, 0), 0L);
	ultrawidelock_sem_give(&g_sem);
	ultrawidelock_sem_give(&g_sem);
	T_EQ("gives saturate at the limit", ultrawidelock_sem_take(&g_sem, 0), 0L);
	T_EQ("only one was kept", ultrawidelock_sem_take(&g_sem, 0), -1L);
	ultrawidelock_sem_give(&g_sem);
	ultrawidelock_sem_reset(&g_sem);
	T_EQ("reset drops the count", ultrawidelock_sem_take(&g_sem, 0), -1L);

	/* The single-thread contract: a timed take advances the clock and a
	 * delayable give lands inside the window. */
	ultrawidelock_dwork_init(&give, dwork_give);
	ultrawidelock_dwork_schedule(&give, 30);
	T_EQ("a give due at 30ms satisfies a 100ms take", ultrawidelock_sem_take(&g_sem, 100), 0L);
	T_EQ("the take consumed the full window", (long)ultrawidelock_osal_host_now_ms(), 100L);

	ultrawidelock_dwork_schedule(&give, 50);
	T_EQ("a give due past the window times out", ultrawidelock_sem_take(&g_sem, 20), -1L);
	T_EQ("the timeout still elapsed", (long)ultrawidelock_osal_host_now_ms(), 120L);
	T_EQ("the late give then satisfies a forever take",
	     ultrawidelock_sem_take(&g_sem, ULTRAWIDELOCK_WAIT_FOREVER), 0L);
}

static void test_thread_and_init(void)
{
	ULTRAWIDELOCK_THREAD_STACK_DEFINE(stack, 512);
	ultrawidelock_thread_t t;
	int flag = 0;

	t_group("osal: thread + init hooks");
	T_EQ("create records and reports success",
	     ultrawidelock_thread_create(&t, stack, ULTRAWIDELOCK_THREAD_STACK_SIZEOF(stack),
					 thread_entry, &flag, ULTRAWIDELOCK_THREAD_PRIO_NORM, "t"),
	     0L);
	T_OK("host threads do not run by themselves", flag == 0 && t.created == 1);
	t.entry(t.arg);
	T_EQ("the suite drives the entry", (long)flag, 1L);

	T_EQ("init hooks run on demand", ultrawidelock_osal_init_all(), 0L);
	T_EQ("the registered hook ran", (long)g_hook_runs, 1L);
	ultrawidelock_osal_init_all();
	T_EQ("init_all is idempotent", (long)g_hook_runs, 1L);
}

static void test_flash(void)
{
	const struct ultrawidelock_flash_area *fa;
	struct ultrawidelock_flash_host_area *host;
	static const uint8_t word[4] = { 0xde, 0xad, 0xca, 0xfe };
	uint8_t back[4];

	t_group("flash: host partitions");
	ultrawidelock_flash_host_reset();

	T_EQ("staging opens", ultrawidelock_flash_open(ULTRAWIDELOCK_FLASH_AREA_STAGING, &fa), 0L);
	T_EQ("staging geometry matches pm_static.yml",
	     (long)ultrawidelock_flash_size(fa), (long)ULTRAWIDELOCK_FLASH_HOST_STAGING_SIZE);
	host = ultrawidelock_flash_host_area(ULTRAWIDELOCK_FLASH_AREA_STAGING);

	T_EQ("aligned write lands", ultrawidelock_flash_write(fa, 8, word, sizeof(word)), 0L);
	T_EQ("the bytes are there", (long)host->buf[8], 0xdeL);
	T_EQ("read round-trips", ultrawidelock_flash_read(fa, 8, back, sizeof(back)), 0L);
	T_OK("read matches write", memcmp(back, word, sizeof(word)) == 0);

	T_EQ("unaligned write offset refused", ultrawidelock_flash_write(fa, 2, word, 4), -1L);
	T_EQ("unaligned write length refused", ultrawidelock_flash_write(fa, 0, word, 3), -1L);
	T_EQ("unaligned erase refused", ultrawidelock_flash_erase(fa, 100, 4096), -1L);
	T_EQ("erase off the end refused",
	     ultrawidelock_flash_erase(fa, 0, ULTRAWIDELOCK_FLASH_HOST_STAGING_SIZE + 4096), -1L);
	T_EQ("write off the end refused",
	     ultrawidelock_flash_write(fa, ULTRAWIDELOCK_FLASH_HOST_STAGING_SIZE - 2, word, 4), -1L);

	T_EQ("page erase succeeds", ultrawidelock_flash_erase(fa, 0, 4096), 0L);
	T_EQ("erase restored 0xff", (long)host->buf[8], 0xffL);

	host->write_fail_in = 1;
	T_EQ("fail_in lets N calls through", ultrawidelock_flash_write(fa, 0, word, 4), 0L);
	T_EQ("then fails every call", ultrawidelock_flash_write(fa, 0, word, 4), -1L);
	T_EQ("still failing", ultrawidelock_flash_write(fa, 0, word, 4), -1L);

	ultrawidelock_flash_close(fa);
	T_EQ("calls were recorded", (long)host->close_calls, 1L);

	host->fail_open = 1;
	T_EQ("fail_open refuses", ultrawidelock_flash_open(ULTRAWIDELOCK_FLASH_AREA_STAGING, &fa), -1L);

	T_EQ("no reboot yet", (long)ultrawidelock_flash_host_reboots(), 0L);
	ultrawidelock_reboot();
	T_EQ("reboot recorded, not taken", (long)ultrawidelock_flash_host_reboots(), 1L);

	ultrawidelock_flash_host_reset();
	host = ultrawidelock_flash_host_area(ULTRAWIDELOCK_FLASH_AREA_STAGING);
	T_OK("reset restores knobs and recorders",
	     host->fail_open == 0 && host->write_fail_in == -1 && host->write_calls == 0);
	T_EQ("primary opens after reset",
	     ultrawidelock_flash_open(ULTRAWIDELOCK_FLASH_AREA_PRIMARY, &fa), 0L);
	T_EQ("primary geometry matches pm_static.yml",
	     (long)ultrawidelock_flash_size(fa), (long)ULTRAWIDELOCK_FLASH_HOST_PRIMARY_SIZE);
}

void test_ultrawidelock_port(void)
{
	test_work();
	test_dwork();
	test_sem();
	test_thread_and_init();
	test_flash();
}
