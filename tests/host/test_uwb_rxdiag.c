/**
 * @file test_uwb_rxdiag.c — RX/TX diagnostic tallies + heartbeat (uwb_rxdiag.c)
 * on the drvfake radio and the host OSAL's virtual clock. The suite calls the
 * uwb_seam.h entry points this file implements and fires the heartbeat by
 * advancing the clock; printk output is diverted to /dev/null while a
 * heartbeat renders. Fake-only: the tallies, chains and arming are proven,
 * the real ISR context is not.
 */
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "drvfake.h"
#include "test.h"
#include "uwb_rxdiag.h"
#include "woz_osal.h"

/* Seam entry points + the init hook (see uwb_seam.h / uwb_rxdiag.c). */
void woz_uwb_set_callbacks(dwt_callbacks_s *callbacks);
int32_t woz_uwb_configure_phy(dwt_config_t *config);
extern int (*const woz_init_rxdiag_init)(void);

/* The MAC-side callbacks our shims must chain into. */
static unsigned chain_rxok, chain_rxto, chain_rxerr, chain_txdone;
static void b_rxok(const dwt_cb_data_t *d)
{
	(void)d;
	chain_rxok++;
}
static void b_rxto(const dwt_cb_data_t *d)
{
	(void)d;
	chain_rxto++;
}
static void b_rxerr(const dwt_cb_data_t *d)
{
	(void)d;
	chain_rxerr++;
}
static void b_txdone(const dwt_cb_data_t *d)
{
	(void)d;
	chain_txdone++;
}

/** Advance the virtual clock with stdout parked on /dev/null. */
static unsigned advance_quiet(int64_t ms)
{
	int saved = dup(1);
	int devnull = open("/dev/null", O_WRONLY);
	unsigned ran;

	fflush(stdout);
	dup2(devnull, 1);
	ran = woz_osal_host_advance_ms(ms);
	fflush(stdout);
	dup2(saved, 1);
	close(saved);
	close(devnull);
	return ran;
}

void test_uwb_rxdiag(void)
{
	uint32_t ok, err, to, tx, lerr, lok;

	t_group("boot init binds the work items and arms the heartbeat");
	drvfake_reset();
	woz_osal_host_reset();
	T_EQ("init rc", woz_init_rxdiag_init(), 0);
	T_OK("stream defaulted on (pretty shell off)", uwb_rxdiag_stream_get());
	T_EQ("not due before 2s", (long)woz_osal_host_advance_ms(1999), 0L);
	T_EQ("armed at 2s", (long)advance_quiet(1), 1L);

	t_group("callback interception");
	drvfake_reset();
	woz_uwb_set_callbacks(NULL);
	T_EQ("NULL table forwarded", (long)drvfake.setcallbacks_calls, 1L);

	dwt_callbacks_s cbs = {0};

	cbs.cbRxOk = b_rxok;
	cbs.cbRxTo = b_rxto;
	cbs.cbRxErr = b_rxerr;
	cbs.cbTxDone = b_txdone;
	woz_uwb_set_callbacks(&cbs);
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

	/* The tallies are file statics with no reset hook, and uwb_isr_register()
	 * installs these same shims, so an earlier suite in this binary may already
	 * have fired events through them. Assert deltas, not absolutes. */
	uint32_t base_ok, base_err, base_to, base_tx;

	uwb_rxdiag_get_counts(&base_ok, &base_err, &base_to, &base_tx, NULL, NULL);

	cbs.cbRxOk(&d);
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, &lerr, &lok);
	T_EQ("rxok tally", (long)(ok - base_ok), 1L);
	T_EQ("ok status latched", (long)lok, (long)0xcafe0001u);
	T_EQ("index tracker fed", (long)drvfake.notify_calls, 1L);
	T_EQ("rx-ok chained", (long)chain_rxok, 1L);
	T_EQ("prepoll decoded after arm", (long)drvfake.try_prepoll_calls, 1L);
	T_EQ("decode got the length", (long)drvfake.last_prepoll_len, 40L);

	drvfake.rx_awaiting = true; /* POLL event: decode must be skipped */
	cbs.cbRxOk(&d);
	T_EQ("no decode while awaiting POLL", (long)drvfake.try_prepoll_calls, 1L);
	drvfake.rx_awaiting = false;

	cbs.cbRxOk(NULL); /* defensive-NULL path */
	uwb_rxdiag_get_counts(&ok, NULL, NULL, NULL, NULL, NULL);
	T_EQ("NULL event still tallied", (long)(ok - base_ok), 3L);
	T_EQ("NULL event not notified", (long)drvfake.notify_calls, 2L);

	t_group("timeout / error / tx-done shims");
	cbs.cbRxTo(&d);
	cbs.cbRxErr(&d);
	cbs.cbTxDone(&d);
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, &lerr, &lok);
	T_EQ("to tally", (long)(to - base_to), 1L);
	T_EQ("err tally", (long)(err - base_err), 1L);
	T_EQ("tx tally", (long)(tx - base_tx), 1L);
	T_EQ("err status latched", (long)lerr, (long)0xcafe0001u);
	T_EQ("rx-to chained", (long)chain_rxto, 1L);
	T_EQ("rx-err chained", (long)chain_rxerr, 1L);
	T_EQ("tx-done chained", (long)chain_txdone, 1L);

	t_group("NULL handlers stay NULL");
	dwt_callbacks_s none = {0};

	woz_uwb_set_callbacks(&none);
	T_OK("all shims elided", none.cbRxOk == NULL && none.cbRxTo == NULL &&
				  none.cbRxErr == NULL && none.cbTxDone == NULL);

	/* Restore a live table from a FRESH copy (cbs already holds the shims;
	 * re-registering it would make the shim chain to itself). */
	dwt_callbacks_s cbs2 = {0};

	cbs2.cbRxOk = b_rxok;
	cbs2.cbRxTo = b_rxto;
	cbs2.cbRxErr = b_rxerr;
	cbs2.cbTxDone = b_txdone;
	woz_uwb_set_callbacks(&cbs2);
	cbs = cbs2; /* the shimmed table the rest of the suite drives */

	t_group("config wraps pass through");
	dwt_config_t cfg = {0};

	cfg.chan = 5;
	T_EQ("configure chained", woz_uwb_configure_phy(&cfg), 0);
	T_EQ("real configure hit", (long)drvfake.configure_calls, 1L);
	T_EQ("NULL configure chained", woz_uwb_configure_phy(NULL), 0);

	t_group("stream toggles drive the heartbeat");
	uwb_rxdiag_stream_set(false);
	T_OK("stream off", !uwb_rxdiag_stream_get());
	T_EQ("cancelled: nothing fires", (long)woz_osal_host_advance_ms(5000), 0L);
	uwb_rxdiag_stream_set(true);
	T_OK("stream on", uwb_rxdiag_stream_get());
	T_EQ("armed immediately", (long)advance_quiet(0), 1L);
	T_EQ("re-armed at the 2s cadence", (long)advance_quiet(2000), 1L);
	uwb_rxdiag_rng_set(true);
	T_OK("rng stream on", uwb_rxdiag_rng_get());
	uwb_rxdiag_rng_set(false);
	T_OK("rng stream off", !uwb_rxdiag_rng_get());

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
	T_EQ("active beat fires", (long)advance_quiet(2000), 1L);
	drvfake.fira_age_ms = 5000; /* stale range branch */
	cbs.cbRxOk(&d);
	(void)advance_quiet(2000);
	drvfake.fira_have = false; /* no-range branch */
	cbs.cbRxOk(&d);
	(void)advance_quiet(2000);
	(void)advance_quiet(2000); /* nothing new: idle announced once */
	(void)advance_quiet(2000); /* still idle: quiet path */
	uwb_rxdiag_stream_set(false);
	T_EQ("no re-arm when stopped", (long)woz_osal_host_advance_ms(4000), 0L);
	T_OK("heartbeat survived all branches", 1);
}
