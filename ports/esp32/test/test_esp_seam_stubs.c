/*
 * Host test for this port's half of uwb_seam.h (components/woz_uwb/port/
 * woz_seam_stubs.c). "Theatre" suite: dwt_setcallbacks, dwt_configure and the
 * ccc_shim_rx_* entry points are recording doubles here, so passing proves the
 * shim's interception + chaining logic (save the MAC's callbacks, install the
 * shims, feed the STS tracker, gate the Pre-POLL decode on awaiting-poll) —
 * not that every caller actually goes through the seam, which is
 * tests/tooling/uwb_seam_check.sh's job. Types come from the real deca_device_api.h.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <deca_device_api.h>

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* ---- the seam entry points + ccc_shim doubles ----------------------------- */
void woz_uwb_set_callbacks(dwt_callbacks_s *callbacks);
int32_t woz_uwb_configure_phy(dwt_config_t *config);
extern uint32_t g_ccc_dbg_decode;

static dwt_callbacks_s s_real_registered;
static int s_real_setcb_calls;

void dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	s_real_setcb_calls++;
	if (callbacks != NULL) {
		s_real_registered = *callbacks;
	} else {
		memset(&s_real_registered, 0, sizeof(s_real_registered));
	}
}

static int s_configure_calls;
static dwt_config_t s_last_cfg;

int32_t dwt_configure(dwt_config_t *config)
{
	s_configure_calls++;
	if (config != NULL) {
		s_last_cfg = *config;
	}
	return 0;
}

static bool s_awaiting, s_deadline;
static uint32_t s_notified_status;
static int s_notify_calls, s_prepoll_calls;
static uint16_t s_prepoll_len;
static bool s_final;

bool ccc_shim_rx_awaiting_poll(void)
{
	return s_awaiting;
}

bool ccc_shim_rx_deadline_pending(void)
{
	return s_deadline;
}

bool ccc_shim_rx_awaiting_final(void)
{
	return s_final;
}

void ccc_shim_rx_notify_rx(uint32_t status)
{
	s_notified_status = status;
	s_notify_calls++;
}

void ccc_shim_rx_try_prepoll(uint16_t datalength)
{
	s_prepoll_len = datalength;
	s_prepoll_calls++;
}

/* Chain counter for the MAC's RX-good callback; declared ahead of the cirdiag
 * double, which records it to pin capture-before-re-arm ordering. */
static int s_chain_rxok;

/* uwb_cirdiag_capture double: shim_rxok latches the CIA diag through it; the
 * real latch lives in the woz_uwb driver, out of this wrap-seam suite. Records
 * deadline_pending, the flag that keeps the windowed-CIR read out of a live
 * ranging block, plus the chain count at call time — on the Final the
 * capture must run BEFORE the MAC re-arms the receiver, because the
 * accumulator is only readable while the radio is idle. */
static int s_cirdiag_calls;
static bool s_cirdiag_deadline;
static bool s_cirdiag_is_final;
static int s_cirdiag_chain_at_call;

/* uwb_cirdiag_window_due double: the shim gates the pre-arm window capture on it, so the
 * suite drives it directly rather than reproducing the decimation counter. */
static bool s_window_due = true;

bool uwb_cirdiag_window_due(void)
{
	return s_window_due;
}

bool uwb_cirdiag_capture(uint32_t status, uint16_t datalength, bool deadline_pending,
			 bool is_final)
{
	(void)status;
	(void)datalength;
	s_cirdiag_calls++;
	s_cirdiag_deadline = deadline_pending;
	s_cirdiag_is_final = is_final;
	s_cirdiag_chain_at_call = s_chain_rxok;
	return false;
}

static int s_chain_rxto, s_chain_rxerr, s_chain_txdone;
static const dwt_cb_data_t *s_chain_rxok_arg;

static void chain_rxok(const dwt_cb_data_t *d)
{
	s_chain_rxok++;
	s_chain_rxok_arg = d;
}

static void chain_rxto(const dwt_cb_data_t *d)
{
	(void)d;
	s_chain_rxto++;
}

static void chain_rxerr(const dwt_cb_data_t *d)
{
	(void)d;
	s_chain_rxerr++;
}

static void chain_txdone(const dwt_cb_data_t *d)
{
	(void)d;
	s_chain_txdone++;
}

int main(void)
{
	printf("-- callback interception --\n");

	dwt_callbacks_s cbs;

	memset(&cbs, 0, sizeof(cbs));
	cbs.cbRxOk = chain_rxok;
	cbs.cbRxTo = chain_rxto;
	cbs.cbRxErr = chain_rxerr;
	cbs.cbTxDone = chain_txdone;

	woz_uwb_set_callbacks(&cbs);
	okc("real registration chained", s_real_setcb_calls == 1);
	okc("all four callbacks replaced by shims",
	    s_real_registered.cbRxOk != NULL && s_real_registered.cbRxOk != chain_rxok &&
	    s_real_registered.cbRxTo != NULL && s_real_registered.cbRxTo != chain_rxto &&
	    s_real_registered.cbRxErr != NULL && s_real_registered.cbRxErr != chain_rxerr &&
	    s_real_registered.cbTxDone != NULL && s_real_registered.cbTxDone != chain_txdone);

	printf("-- RX-good shim: STS tracker + Pre-POLL gate --\n");

	dwt_cb_data_t d;

	memset(&d, 0, sizeof(d));
	d.status = 0x12345678u;
	d.datalength = 36;

	/* Not awaiting the POLL: notify, chain to the MAC, then decode. */
	s_awaiting = false;
	s_real_registered.cbRxOk(&d);
	okc("tracker fed the status", s_notify_calls == 1 && s_notified_status == 0x12345678u);
	okc("rxok chained", s_chain_rxok == 1 && s_chain_rxok_arg == &d);
	okc("prepoll decode ran", s_prepoll_calls == 1 && s_prepoll_len == 36);
	/* Not the Final: the capture runs AFTER the MAC re-arms, summary only. */
	okc("non-final: cirdiag deferred past the arm, summary only",
	    s_cirdiag_calls == 1 && s_cirdiag_deadline && !s_cirdiag_is_final &&
	    s_cirdiag_chain_at_call == 1);

	/* Awaiting the POLL: the RX is the POLL itself, no Pre-POLL decode. */
	s_awaiting = true;
	s_real_registered.cbRxOk(&d);
	okc("awaiting poll: no prepoll decode",
	    s_prepoll_calls == 1 && s_notify_calls == 2 && s_chain_rxok == 2);
	s_awaiting = false;

	/* The Final: sampled before the arm, awaiting_final is set. The capture must run ahead of
	 * the MAC (chain count still at its pre-call value) and be cleared for the window read —
	 * that pre-arm instant is the only time the radio is idle. */
	s_final = true;
	int chain_before = s_chain_rxok;

	s_real_registered.cbRxOk(&d);
	okc("final: cirdiag captured before the re-arm",
	    s_cirdiag_calls == 3 && s_cirdiag_chain_at_call == chain_before);
	okc("final: window read allowed", !s_cirdiag_deadline && s_cirdiag_is_final);
	/* Decimation: a Final whose window is not due falls back to the post-arm summary, so the
	 * three blocks in four that skip the read keep ranging untouched. It must STILL be marked
	 * final: the FINAL_ONLY latch keys on that flag, and a summary path that dropped it here
	 * is exactly how the first mlgate walk latched zero receptions (2026-08-07). */
	s_window_due = false;
	int cir_before = s_cirdiag_calls;

	s_real_registered.cbRxOk(&d);
	okc("final, window not due: summary after the arm, still marked final",
	    s_cirdiag_calls == cir_before + 1 && s_cirdiag_deadline && s_cirdiag_is_final &&
	    s_cirdiag_chain_at_call == s_chain_rxok);
	s_window_due = true;
	s_final = false;

	/* The MAC's handler leaves a POLL or Final RX armed behind a live block; cirdiag must
	 * hear about it so it skips the windowed-CIR read for that reception. */
	s_deadline = true;
	cir_before = s_cirdiag_calls;
	s_real_registered.cbRxOk(&d);
	okc("live block: cirdiag told a deadline is pending",
	    s_cirdiag_calls == cir_before + 1 && s_cirdiag_deadline);
	s_deadline = false;

	/* NULL event data: the chain still runs, no tracker feed, no decode. */
	int chain_n = s_chain_rxok, notify_n = s_notify_calls, prepoll_n = s_prepoll_calls;

	s_real_registered.cbRxOk(NULL);
	okc("NULL data tolerated", s_chain_rxok == chain_n + 1 && s_notify_calls == notify_n &&
					   s_prepoll_calls == prepoll_n);

	printf("-- passthrough shims --\n");

	s_real_registered.cbRxTo(&d);
	s_real_registered.cbRxErr(&d);
	s_real_registered.cbTxDone(&d);
	okc("rxto/rxerr/txdone chain",
	    s_chain_rxto == 1 && s_chain_rxerr == 1 && s_chain_txdone == 1);

	dwt_config_t cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.chan = 9;
	okc("phy configure passthrough", woz_uwb_configure_phy(&cfg) == 0 &&
					 s_configure_calls == 1 && s_last_cfg.chan == 9);

	okc("diag decode counter defined (stays 0)", g_ccc_dbg_decode == 0);

	printf("-- NULL registrations --\n");

	/* Individual NULL members stay NULL (no shim installed over nothing). */
	memset(&cbs, 0, sizeof(cbs));
	cbs.cbRxOk = chain_rxok;
	woz_uwb_set_callbacks(&cbs);
	okc("only rxok shimmed",
	    s_real_registered.cbRxOk != NULL && s_real_registered.cbRxTo == NULL &&
	    s_real_registered.cbRxErr == NULL && s_real_registered.cbTxDone == NULL);

	/* All-NULL callbacks: shims must not fire the stale chain pointers. */
	memset(&cbs, 0, sizeof(cbs));
	woz_uwb_set_callbacks(&cbs);
	okc("all-NULL registration passes through",
	    s_real_registered.cbRxOk == NULL && s_real_registered.cbTxDone == NULL);

	/* NULL table forwards untouched. */
	woz_uwb_set_callbacks(NULL);
	okc("NULL table forwarded", s_real_setcb_calls == 4);

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
