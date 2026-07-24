/**
 * @file test_uwb_isr.c — DW3000 callback registration (uwb_isr.c) on the
 * drvfake radio. Pins the callback table wiring, the unmask bit set, and each
 * handler's peek/re-arm behaviour. The dwt_cb_data_t events are synthesized —
 * no interrupt controller is involved.
 */
#include "drvfake.h"
#include "test.h"
#include "uwb_isr.h"

void test_uwb_isr(void)
{
	t_group("registration");
	drvfake_reset();
	T_EQ("register rc", uwb_isr_register(), 0);
	T_EQ("table handed to driver", (long)drvfake.setcallbacks_calls, 1L);
	T_OK("rx-ok wired", drvfake.cbs.cbRxOk != NULL);
	T_OK("rx-to wired", drvfake.cbs.cbRxTo != NULL);
	T_OK("rx-err wired", drvfake.cbs.cbRxErr != NULL);
	T_OK("tx-done wired", drvfake.cbs.cbTxDone != NULL);
	T_OK("spi handlers absent", drvfake.cbs.cbSPIErr == NULL && drvfake.cbs.cbSPIRdy == NULL);
	T_EQ("unmask call", (long)drvfake.setinterrupt_calls, 1L);
	T_EQ("unmask bits", (long)drvfake.last_int_lo,
	     (long)(DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK |
		    DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK |
		    DWT_INT_RXSTO_BIT_MASK | DWT_INT_ARFE_BIT_MASK | DWT_INT_CIAERR_BIT_MASK |
		    DWT_INT_TXFRS_BIT_MASK));

	t_group("rx-good: peek capped at 8 bytes, then re-arm");
	dwt_cb_data_t d = {0};

	d.datalength = 24;
	d.status = 0x1234u;
	drvfake.cbs.cbRxOk(&d);
	T_EQ("frame peeked", (long)drvfake.readrxdata_calls, 1L);
	T_EQ("peek is 8 bytes", (long)drvfake.last_readrx_len, 8L);
	T_EQ("rx re-armed", (long)drvfake.rxenable_calls, 1L);

	d.datalength = 3; /* shorter than the peek window */
	drvfake.cbs.cbRxOk(&d);
	T_EQ("short peek", (long)drvfake.last_readrx_len, 3L);

	d.datalength = 0; /* zero-length event: no read at all */
	drvfake.cbs.cbRxOk(&d);
	T_EQ("no peek on empty frame", (long)drvfake.readrxdata_calls, 2L);
	T_EQ("still re-armed", (long)drvfake.rxenable_calls, 3L);

	t_group("timeout / error / tx-done");
	drvfake.cbs.cbRxTo(&d);
	T_EQ("timeout re-arms", (long)drvfake.rxenable_calls, 4L);
	drvfake.cbs.cbRxErr(&d);
	T_EQ("error re-arms", (long)drvfake.rxenable_calls, 5L);
	drvfake.cbs.cbTxDone(&d);
	T_EQ("tx-done does not re-arm", (long)drvfake.rxenable_calls, 5L);
}
