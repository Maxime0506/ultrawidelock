/**
 * @file test_uwb_rxdiag.c — RX/TX diagnostic tallies + heartbeat (uwb_rxdiag.c)
 * on the drvfake radio and the logfake k_work surface. The suite calls the
 * __wrap_* entry points directly (no ld --wrap on host) and fires the
 * heartbeat work item by hand; printk output is diverted to /dev/null while a
 * heartbeat renders. Fake-only: the tallies and chains are proven, the timing
 * (2 s cadence, real ISR context) is not.
 */
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <zephyr/kernel.h>

#include "drvfake.h"
#include "test.h"
#include "uwb_rxdiag.h"

/* --wrap entry points + SYS_INIT hook (see uwb_rxdiag.c / logfake init.h). */
void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks);
int32_t __wrap_dwt_configure(dwt_config_t *config);
void __wrap_dwt_configurestsmode(uint8_t stsMode);
extern int (*const logfake_sys_init_rxdiag_init)(void);

/* The blob-side callbacks our shims must chain into. */
static unsigned blob_rxok, blob_rxto, blob_rxerr, blob_txdone;
static void b_rxok(const dwt_cb_data_t *d)
{
	(void)d;
	blob_rxok++;
}
static void b_rxto(const dwt_cb_data_t *d)
{
	(void)d;
	blob_rxto++;
}
static void b_rxerr(const dwt_cb_data_t *d)
{
	(void)d;
	blob_rxerr++;
}
static void b_txdone(const dwt_cb_data_t *d)
{
	(void)d;
	blob_txdone++;
}

/** Fire the last-scheduled work item with stdout parked on /dev/null. */
static void fire_quiet(void)
{
	int saved = dup(1);
	int devnull = open("/dev/null", O_WRONLY);

	fflush(stdout);
	dup2(devnull, 1);
	workfake.last->work.handler(&workfake.last->work);
	fflush(stdout);
	dup2(saved, 1);
	close(saved);
	close(devnull);
}

void test_uwb_rxdiag(void)
{
	uint32_t ok, err, to, tx, lerr, lok;

	t_group("callback interception");
	drvfake_reset();
	__wrap_dwt_setcallbacks(NULL);
	T_EQ("NULL table forwarded", (long)drvfake.real_setcallbacks_calls, 1L);

	dwt_callbacks_s cbs = {0};

	cbs.cbRxOk = b_rxok;
	cbs.cbRxTo = b_rxto;
	cbs.cbRxErr = b_rxerr;
	cbs.cbTxDone = b_txdone;
	__wrap_dwt_setcallbacks(&cbs);
	T_OK("rx-ok shimmed", cbs.cbRxOk != NULL && cbs.cbRxOk != b_rxok);
	T_OK("rx-to shimmed", cbs.cbRxTo != NULL && cbs.cbRxTo != b_rxto);
	T_OK("rx-err shimmed", cbs.cbRxErr != NULL && cbs.cbRxErr != b_rxerr);
	T_OK("tx-done shimmed", cbs.cbTxDone != NULL && cbs.cbTxDone != b_txdone);

	t_group("rx-good shim: tally, notify, then decode after the arm");
	dwt_cb_data_t d = {0};

	d.status = 0xcafe0001u;
	d.datalength = 40;
	drvfake.rx_awaiting = false;
	drvfake.cbs.cbRxOk = NULL; /* isolate: use the shimmed table copy */
	cbs.cbRxOk(&d);
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, &lerr, &lok);
	T_EQ("rxok tally", (long)ok, 1L);
	T_EQ("ok status latched", (long)lok, (long)0xcafe0001u);
	T_EQ("index tracker fed", (long)drvfake.notify_calls, 1L);
	T_EQ("blob chained", (long)blob_rxok, 1L);
	T_EQ("prepoll decoded after arm", (long)drvfake.try_prepoll_calls, 1L);
	T_EQ("decode got the length", (long)drvfake.last_prepoll_len, 40L);

	drvfake.rx_awaiting = true; /* POLL event: decode must be skipped */
	cbs.cbRxOk(&d);
	T_EQ("no decode while awaiting POLL", (long)drvfake.try_prepoll_calls, 1L);
	drvfake.rx_awaiting = false;

	cbs.cbRxOk(NULL); /* defensive-NULL path */
	uwb_rxdiag_get_counts(&ok, NULL, NULL, NULL, NULL, NULL);
	T_EQ("NULL event still tallied", (long)ok, 3L);
	T_EQ("NULL event not notified", (long)drvfake.notify_calls, 2L);

	t_group("timeout / error / tx-done shims");
	cbs.cbRxTo(&d);
	cbs.cbRxErr(&d);
	cbs.cbTxDone(&d);
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, &lerr, &lok);
	T_EQ("to tally", (long)to, 1L);
	T_EQ("err tally", (long)err, 1L);
	T_EQ("tx tally", (long)tx, 1L);
	T_EQ("err status latched", (long)lerr, (long)0xcafe0001u);
	T_EQ("blob to chained", (long)blob_rxto, 1L);
	T_EQ("blob err chained", (long)blob_rxerr, 1L);
	T_EQ("blob txdone chained", (long)blob_txdone, 1L);

	t_group("NULL blob handlers stay NULL");
	dwt_callbacks_s none = {0};

	__wrap_dwt_setcallbacks(&none);
	T_OK("all shims elided", none.cbRxOk == NULL && none.cbRxTo == NULL &&
				  none.cbRxErr == NULL && none.cbTxDone == NULL);

	/* Restore a live table from a FRESH copy (cbs already holds the shims;
	 * re-registering it would make the shim chain to itself). */
	dwt_callbacks_s cbs2 = {0};

	cbs2.cbRxOk = b_rxok;
	cbs2.cbRxTo = b_rxto;
	cbs2.cbRxErr = b_rxerr;
	cbs2.cbTxDone = b_txdone;
	__wrap_dwt_setcallbacks(&cbs2);
	cbs = cbs2; /* the shimmed table the rest of the suite drives */

	t_group("config wraps pass through");
	dwt_config_t cfg = {0};

	cfg.chan = 5;
	T_EQ("configure chained", __wrap_dwt_configure(&cfg), 0);
	T_EQ("real configure hit", (long)drvfake.real_configure_calls, 1L);
	T_EQ("NULL configure chained", __wrap_dwt_configure(NULL), 0);
	__wrap_dwt_configurestsmode(0x13);
	T_EQ("stsmode chained", (long)drvfake.real_stsmode_calls, 1L);
	T_EQ("stsmode value", (long)drvfake.last_stsmode, 0x13L);

	t_group("stream toggles drive the work item");
	workfake.last = NULL;
	uwb_rxdiag_stream_set(true);
	T_OK("stream on", uwb_rxdiag_stream_get());
	T_OK("heartbeat armed now", workfake.last != NULL && workfake.last_delay == 0);
	uwb_rxdiag_stream_set(false);
	T_OK("stream off", !uwb_rxdiag_stream_get());
	T_EQ("heartbeat cancelled", (long)workfake.cancel_calls, 1L);
	uwb_rxdiag_rng_set(true);
	T_OK("rng stream on", uwb_rxdiag_rng_get());
	uwb_rxdiag_rng_set(false);
	T_OK("rng stream off", !uwb_rxdiag_rng_get());

	t_group("boot init arms the heartbeat (pretty shell off)");
	unsigned resched0 = workfake.reschedule_calls;

	T_EQ("init rc", logfake_sys_init_rxdiag_init(), 0);
	T_OK("armed at 2s", workfake.reschedule_calls == resched0 + 1 &&
				    workfake.last_delay == 2000);
	T_OK("stream defaulted on", uwb_rxdiag_stream_get());

	t_group("heartbeat: active, fresh + stale + idle branches");
	/* Spread RX detections across two real-time cadence bins (2 ms apart) so
	 * the heartbeat's peak/second-peak scan sees two populated bins. */
	{
		struct timespec t0, t1;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		cbs.cbRxOk(&d);
		cbs.cbRxOk(&d);
		do {
			clock_gettime(CLOCK_MONOTONIC, &t1);
		} while ((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec) <
			 2100000L);
		/* equal counts in the second bin: the later-visited of the two
		 * equal bins always exercises the second-peak branch */
		cbs.cbRxOk(&d);
		cbs.cbRxOk(&d);
	}
	drvfake.fira_have = true;
	drvfake.fira_cm = 123;
	drvfake.fira_age_ms = 400; /* fresh */
	drvfake.ccc_active = true;
	cbs.cbRxOk(&d); /* new good frame since last beat -> active */
	fire_quiet();
	T_OK("re-armed while streaming", workfake.last_delay == 2000);
	drvfake.fira_age_ms = 5000; /* stale range branch */
	cbs.cbRxOk(&d);
	fire_quiet();
	drvfake.fira_have = false; /* no-range branch */
	cbs.cbRxOk(&d);
	fire_quiet();
	fire_quiet(); /* nothing new: idle announced once */
	fire_quiet(); /* still idle: quiet path */
	uwb_rxdiag_stream_set(false);
	fire_quiet(); /* not streaming: no re-arm */
	T_EQ("no re-arm when stopped", (long)workfake.cancel_calls, 2L);
	T_OK("heartbeat survived all branches", 1);
}
