/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host test for the ESP32 --wrap RX-callback shim (components/woz_uwb/port/
 * woz_wrap_stubs.c). "Theatre" suite: __real_dwt_setcallbacks and the
 * ccc_shim_rx_* entry points are recording doubles here, so passing proves the
 * shim's interception + chaining logic (save the blob's callbacks, install the
 * shims, feed the STS tracker, gate the Pre-POLL decode on awaiting-poll) —
 * not the ld --wrap seam itself, which only exists in the on-target link and
 * is guarded by verify_port.sh. Types come from the real deca_device_api.h.
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

/* ---- the wrap seam + ccc_shim doubles ------------------------------------ */
void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks);
void __wrap_dwt_configurestsmode(uint8_t stsMode);
extern uint32_t g_ccc_dbg_decode;

static dwt_callbacks_s s_real_registered;
static int s_real_setcb_calls;

void __real_dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	s_real_setcb_calls++;
	if (callbacks != NULL) {
		s_real_registered = *callbacks;
	} else {
		memset(&s_real_registered, 0, sizeof(s_real_registered));
	}
}

static uint8_t s_sts_mode = 0xFF;

void __real_dwt_configurestsmode(uint8_t stsMode)
{
	s_sts_mode = stsMode;
}

static bool s_awaiting;
static uint32_t s_notified_status;
static int s_notify_calls, s_prepoll_calls;
static uint16_t s_prepoll_len;

bool ccc_shim_rx_awaiting_poll(void)
{
	return s_awaiting;
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

/* ---- the blob's own callbacks (recording) -------------------------------- */
static int s_blob_rxok, s_blob_rxto, s_blob_rxerr, s_blob_txdone;
static const dwt_cb_data_t *s_blob_rxok_arg;

static void blob_rxok(const dwt_cb_data_t *d)
{
	s_blob_rxok++;
	s_blob_rxok_arg = d;
}

static void blob_rxto(const dwt_cb_data_t *d)
{
	(void)d;
	s_blob_rxto++;
}

static void blob_rxerr(const dwt_cb_data_t *d)
{
	(void)d;
	s_blob_rxerr++;
}

static void blob_txdone(const dwt_cb_data_t *d)
{
	(void)d;
	s_blob_txdone++;
}

int main(void)
{
	printf("-- callback interception --\n");

	dwt_callbacks_s cbs;

	memset(&cbs, 0, sizeof(cbs));
	cbs.cbRxOk = blob_rxok;
	cbs.cbRxTo = blob_rxto;
	cbs.cbRxErr = blob_rxerr;
	cbs.cbTxDone = blob_txdone;

	__wrap_dwt_setcallbacks(&cbs);
	okc("real registration chained", s_real_setcb_calls == 1);
	okc("all four callbacks replaced by shims",
	    s_real_registered.cbRxOk != NULL && s_real_registered.cbRxOk != blob_rxok &&
	    s_real_registered.cbRxTo != NULL && s_real_registered.cbRxTo != blob_rxto &&
	    s_real_registered.cbRxErr != NULL && s_real_registered.cbRxErr != blob_rxerr &&
	    s_real_registered.cbTxDone != NULL && s_real_registered.cbTxDone != blob_txdone);

	printf("-- RX-good shim: STS tracker + Pre-POLL gate --\n");

	dwt_cb_data_t d;

	memset(&d, 0, sizeof(d));
	d.status = 0x12345678u;
	d.datalength = 36;

	/* Not awaiting the POLL: notify, chain to the blob, then decode. */
	s_awaiting = false;
	s_real_registered.cbRxOk(&d);
	okc("tracker fed the status", s_notify_calls == 1 && s_notified_status == 0x12345678u);
	okc("blob rxok chained", s_blob_rxok == 1 && s_blob_rxok_arg == &d);
	okc("prepoll decode ran", s_prepoll_calls == 1 && s_prepoll_len == 36);

	/* Awaiting the POLL: the RX is the POLL itself, no Pre-POLL decode. */
	s_awaiting = true;
	s_real_registered.cbRxOk(&d);
	okc("awaiting poll: no prepoll decode",
	    s_prepoll_calls == 1 && s_notify_calls == 2 && s_blob_rxok == 2);
	s_awaiting = false;

	/* NULL event data: blob still chains, no tracker feed, no decode. */
	s_real_registered.cbRxOk(NULL);
	okc("NULL data tolerated",
	    s_blob_rxok == 3 && s_notify_calls == 2 && s_prepoll_calls == 1);

	printf("-- passthrough shims --\n");

	s_real_registered.cbRxTo(&d);
	s_real_registered.cbRxErr(&d);
	s_real_registered.cbTxDone(&d);
	okc("rxto/rxerr/txdone chain",
	    s_blob_rxto == 1 && s_blob_rxerr == 1 && s_blob_txdone == 1);

	__wrap_dwt_configurestsmode(3);
	okc("sts mode passthrough", s_sts_mode == 3);

	okc("diag decode counter defined (stays 0)", g_ccc_dbg_decode == 0);

	printf("-- NULL registrations --\n");

	/* Individual NULL members stay NULL (no shim installed over nothing). */
	memset(&cbs, 0, sizeof(cbs));
	cbs.cbRxOk = blob_rxok;
	__wrap_dwt_setcallbacks(&cbs);
	okc("only rxok shimmed",
	    s_real_registered.cbRxOk != NULL && s_real_registered.cbRxTo == NULL &&
	    s_real_registered.cbRxErr == NULL && s_real_registered.cbTxDone == NULL);

	/* All-NULL callbacks: shims must not fire the stale blob pointers. */
	memset(&cbs, 0, sizeof(cbs));
	__wrap_dwt_setcallbacks(&cbs);
	okc("all-NULL registration passes through",
	    s_real_registered.cbRxOk == NULL && s_real_registered.cbTxDone == NULL);

	/* NULL table forwards untouched. */
	__wrap_dwt_setcallbacks(NULL);
	okc("NULL table forwarded", s_real_setcb_calls == 4);

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
