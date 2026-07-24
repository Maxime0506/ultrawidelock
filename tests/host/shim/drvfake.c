/* Recording doubles for the driver test binary (see drvfake.h). Linked only by
 * tests/host/run.sh's host_test_drv build — never by the main host binary,
 * whose equivalents live in dw_rx_stub.c. Nothing here computes anything: the
 * doubles record arguments and serve knob values, so the suites prove the
 * driver sources' branch logic against a fake radio, not hardware truth. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "drvfake.h"

#include "ccc_shim.h"
#include "fira_session.h"
#include "woz_uwb_facade.h"

#include <dw3000_hw.h>
#include <dw3000_spi.h>
#include <deca_probe_interface.h>

#include <zephyr/kernel.h>

struct drvfake_state drvfake;

void drvfake_reset(void)
{
	memset(&drvfake, 0, sizeof(drvfake));
	drvfake.stsq_val = 0;
	drvfake.start_aliro_ret = 0;
}

/* ── per-frame diag gate (defined by ccc_shim_rx.c on the main binary) ─────── */
volatile int woz_uwb_diag_on = 1;

/* ── decadriver doubles ────────────────────────────────────────────────────── */
const struct dwt_probe_s dw3000_probe_interf = {0};

int32_t dwt_probe(struct dwt_probe_s *probe_interf)
{
	(void)probe_interf;
	drvfake.probe_calls++;
	if (drvfake.probe_fail_times > 0) {
		drvfake.probe_fail_times--;
		return DWT_ERROR;
	}
	return DWT_SUCCESS;
}

int32_t dwt_initialise(int32_t mode)
{
	(void)mode;
	drvfake.initialise_calls++;
	return drvfake.initialise_ret;
}

uint32_t dwt_readdevid(void)
{
	return drvfake.devid;
}

int32_t dwt_configure(dwt_config_t *config)
{
	drvfake.configure_calls++;
	if (config != NULL) {
		drvfake.last_cfg = *config;
	}
	return drvfake.configure_ret;
}

void dwt_configuretxrf(dwt_txconfig_t *config)
{
	drvfake.configuretxrf_calls++;
	if (config != NULL) {
		drvfake.last_txcfg = *config;
	}
}

void dwt_configuresleep(uint16_t mode, uint8_t wake)
{
	drvfake.configuresleep_calls++;
	drvfake.sleep_mode = mode;
	drvfake.sleep_wake = wake;
}

void dwt_setleds(uint8_t mode)
{
	drvfake.setleds_calls++;
	drvfake.leds_mode = mode;
}

uint32_t dwt_readsysstatuslo(void)
{
	if (drvfake.status_n == 0) {
		return 0u;
	}
	if (drvfake.status_i < drvfake.status_n) {
		return drvfake.status_seq[drvfake.status_i++];
	}
	return drvfake.status_seq[drvfake.status_n - 1];
}

void dwt_writesysstatuslo(uint32_t mask)
{
	drvfake.write_status_calls++;
	drvfake.last_write_status = mask;
}

int32_t dwt_writetxdata(uint16_t txDataLength, uint8_t *txDataBytes, uint16_t txBufferOffset)
{
	(void)txDataLength;
	(void)txDataBytes;
	(void)txBufferOffset;
	drvfake.writetxdata_calls++;
	return drvfake.writetxdata_ret;
}

void dwt_writetxfctrl(uint16_t txFrameLength, uint16_t txBufferOffset, uint8_t ranging)
{
	(void)txBufferOffset;
	(void)ranging;
	drvfake.writetxfctrl_calls++;
	drvfake.last_txfctrl_len = txFrameLength;
}

int32_t dwt_starttx(int32_t mode)
{
	(void)mode;
	drvfake.starttx_calls++;
	return drvfake.starttx_ret;
}

int32_t dwt_rxenable(int32_t mode)
{
	(void)mode;
	drvfake.rxenable_calls++;
	return drvfake.rxenable_ret;
}

void dwt_forcetrxoff(void)
{
	drvfake.trxoff_calls++;
}

void dwt_setrxtimeout(uint32_t time)
{
	drvfake.setrxtimeout_calls++;
	drvfake.last_rxtimeout = time;
}

void dwt_setinterrupt(uint32_t bitmask_lo, uint32_t bitmask_hi, int options)
{
	(void)bitmask_hi;
	(void)options;
	drvfake.setinterrupt_calls++;
	drvfake.last_int_lo = bitmask_lo;
}

void dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	drvfake.setcallbacks_calls++;
	if (callbacks != NULL) {
		drvfake.cbs = *callbacks;
	}
}

void dwt_configurestskey(dwt_sts_cp_key_t *key)
{
	(void)key;
	drvfake.stskey_calls++;
}

void dwt_configurestsiv(dwt_sts_cp_iv_t *iv)
{
	(void)iv;
	drvfake.stsiv_calls++;
}

void dwt_configurestsloadiv(void)
{
	drvfake.loadiv_calls++;
}

void dwt_readrxdata(uint8_t *buffer, uint16_t length, uint16_t rxBufferOffset)
{
	(void)rxBufferOffset;
	drvfake.readrxdata_calls++;
	drvfake.last_readrx_len = length;
	if (length > sizeof(drvfake.rxdata)) {
		length = sizeof(drvfake.rxdata);
	}
	memcpy(buffer, drvfake.rxdata, length);
}

int dwt_readstsquality(int16_t *rxStsQualityIndex, int stsSegment)
{
	(void)stsSegment;
	if (rxStsQualityIndex != NULL) {
		*rxStsQualityIndex = drvfake.stsq_val;
	}
	return drvfake.stsq_ret;
}

uint32_t dwt_read_reg(uint32_t addr)
{
	(void)addr;
	return drvfake.read_reg_val;
}

uint32_t dwt_readsystimestamphi32(void)
{
	return 0u;
}

/* ── ld --wrap bypasses (uwb_rxdiag.c's __wrap_* chain into these) ─────────── */
void __real_dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	drvfake.real_setcallbacks_calls++;
	if (callbacks != NULL) {
		drvfake.cbs = *callbacks;
	}
}

int32_t __real_dwt_configure(dwt_config_t *config)
{
	drvfake.real_configure_calls++;
	if (config != NULL) {
		drvfake.last_cfg = *config;
	}
	return drvfake.configure_ret;
}

void __real_dwt_configurestsmode(uint8_t stsMode)
{
	drvfake.real_stsmode_calls++;
	drvfake.last_stsmode = stsMode;
}

/* ── dw3000 platform glue doubles ──────────────────────────────────────────── */
int dw3000_hw_init(void)
{
	drvfake.hw_init_calls++;
	return 0;
}

void dw3000_hw_reset(void)
{
	drvfake.hw_reset_calls++;
}

int dw3000_hw_init_interrupt(void)
{
	drvfake.hw_irq_calls++;
	return 0;
}

void dw3000_spi_wakeup(void)
{
	drvfake.spi_wakeup_calls++;
}

int32_t dw3000_spi_read(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength,
			uint8_t *readBuffer)
{
	(void)headerLength;
	(void)headerBuffer;
	drvfake.spi_read_calls++;
	if (readLength > sizeof(drvfake.spi_devid)) {
		readLength = sizeof(drvfake.spi_devid);
	}
	memcpy(readBuffer, drvfake.spi_devid, readLength);
	return 0;
}

/* ── ccc_shim fakes (the real shim lives in the main binary's suites) ──────── */
bool ccc_shim_active(void)
{
	return drvfake.ccc_active;
}

void ccc_shim_wrap_log_reset(void)
{
	drvfake.wrap_log_reset_calls++;
}

bool ccc_shim_rx_awaiting_poll(void)
{
	return drvfake.rx_awaiting;
}

void ccc_shim_rx_notify_rx(uint32_t status)
{
	drvfake.notify_calls++;
	drvfake.last_notify_status = status;
}

void ccc_shim_rx_try_prepoll(uint16_t datalength)
{
	drvfake.try_prepoll_calls++;
	drvfake.last_prepoll_len = datalength;
}

/* ── fira_session fakes ────────────────────────────────────────────────────── */
bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out, uint8_t *nlos_out,
			     uint32_t *block_out, int64_t *age_ms_out)
{
	if (!drvfake.fira_have) {
		return false;
	}
	if (cm_out) {
		*cm_out = drvfake.fira_cm;
	}
	if (addr_out) {
		*addr_out = drvfake.fira_addr;
	}
	if (nlos_out) {
		*nlos_out = drvfake.fira_nlos;
	}
	if (block_out) {
		*block_out = drvfake.fira_block;
	}
	if (age_ms_out) {
		*age_ms_out = drvfake.fira_age_ms;
	}
	return true;
}

bool fira_session_range_trusted(void)
{
	return drvfake.fira_trusted;
}

const uint8_t *fira_session_get_ursk(void)
{
	return drvfake.fira_ursk;
}

/* ── woz_uwb_facade fake (uwb_selftest boot path) ──────────────────────────── */
int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *cfg)
{
	drvfake.start_aliro_calls++;
	if (cfg != NULL) {
		drvfake.last_aliro_cfg = *cfg;
		if (cfg->ursk != NULL) {
			memcpy(drvfake.last_aliro_ursk, cfg->ursk, 32);
		}
	}
	return drvfake.start_aliro_ret;
}

/* ── k_work fakes (declared in logfake <zephyr/kernel.h>) ──────────────────── */
struct workfake_state workfake;

int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	workfake.reschedule_calls++;
	workfake.last = dwork;
	workfake.last_delay = delay;
	return 0;
}

int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	workfake.schedule_calls++;
	workfake.last = dwork;
	workfake.last_delay = delay;
	return 0;
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	(void)dwork;
	workfake.cancel_calls++;
	return 0;
}

void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler)
{
	dwork->work.handler = handler;
}

/* ── shell fake (zephyr/shell/shell.h capture sink) ────────────────────────── */
char shellfake_out[8192];
unsigned long shellfake_len;

void shellfake_reset(void)
{
	shellfake_len = 0;
	shellfake_out[0] = '\0';
}

void shellfake_print(const struct shell *sh, const char *fmt, ...)
{
	va_list ap;
	int n;

	(void)sh;
	if (shellfake_len >= sizeof(shellfake_out) - 2) {
		return;
	}
	va_start(ap, fmt);
	n = vsnprintf(shellfake_out + shellfake_len, sizeof(shellfake_out) - 1 - shellfake_len,
		      fmt, ap);
	va_end(ap);
	if (n > 0) {
		shellfake_len += (unsigned long)n;
		if (shellfake_len > sizeof(shellfake_out) - 2) {
			shellfake_len = sizeof(shellfake_out) - 2;
		}
	}
	shellfake_out[shellfake_len++] = '\n';
	shellfake_out[shellfake_len] = '\0';
}
