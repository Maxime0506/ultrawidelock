/* UltraWideLockNfc backend driving an NXP PN532 reader.
 *
 * A dedicated thread owns the chip: it runs the discovery loop (RF field on,
 * one Apple ECP broadcast, one 106 kbps type A activation attempt, field off,
 * sleep) and, once an ISO-DEP User Device is activated, performs the blocking
 * APDU round trips. Stack callbacks (CreateSession / HandleSessionData /
 * DestroySession) are posted to the Aliro workqueue so the stack observes the
 * same threading as with the upstream RFAL transport, and Send() stays
 * asynchronous: it hands the APDU to the thread and returns.
 *
 * The ECP frame layout mirrors src/nfc_prop_ecp.cpp (the RFAL-path emitter):
 * 8-byte Aliro ECP v2 header, 8-byte provisioned reader identifier, CRC_A.
 * The PN532 cannot inject raw frames mid-discovery the way RFAL's proprietary
 * poll hook can, so the frame is broadcast with InCommunicateThru while the
 * CIU CRC is switched off, between activation attempts — the same cadence a
 * matching iPhone expects: ECP beacon, then WUPA.
 */

#include <ultrawidelock_nfc/transport.h>

#include "pn532.h"
#include "pn532_apdu.h"
#include <ultrawidelock_nfc/pn532_bus.h>

#include <aliro/aliro.h>
#include <aliro/connection_handle.h>
#include <aliro_workqueue/aliro_workqueue.h>
#include <reader_storage/reader.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <array>
#include <cstring>

LOG_MODULE_REGISTER(ultrawidelock_nfc_pn532, CONFIG_ULTRAWIDELOCK_NFC_LOG_LEVEL);

namespace
{

/* Parity with the RFAL transport's APDU ceiling (CONFIG_RFAL_FEATURE_ISO_DEP_APDU_MAX_LEN). */
constexpr size_t kApduBufferSize = 512;

constexpr size_t kEcpFrameLen = 18;
constexpr size_t kReaderIdLen = 8;
/* ECP v2 header for the Aliro (Unified Access) profile — keep in sync with
 * src/nfc_prop_ecp.cpp. */
constexpr std::array<uint8_t, 8> kAliroEcpHeader = {0x6A, 0x02, 0xCB, 0x02, 0x06, 0x20, 0x42, 0x20};

/* RFConfiguration fTimeout codes (timeout = 100 us * 2^(code - 1)). The ECP
 * broadcast never gets an answer, so the chip-side wait is shrunk around it;
 * APDU exchanges use a generous per-frame wait (WTX extensions restart it). */
constexpr uint8_t kAtrResTimeoutCode = 0x0B;   /* 102 ms */
constexpr uint8_t kEcpTimeoutCode = 0x04;      /* 0.8 ms */
constexpr uint8_t kExchangeTimeoutCode = 0x0D; /* 410 ms per frame wait */

K_THREAD_STACK_DEFINE(sThreadStack, CONFIG_ULTRAWIDELOCK_NFC_PN532_THREAD_STACK_SIZE);
k_thread sThreadData;
k_tid_t sThreadId;

K_SEM_DEFINE(sWakeSem, 0, 1);

pn532 sChip;
bool sInitDone;

atomic_t sStarted;
atomic_t sTerminateReq;
atomic_t sTagActive;
atomic_t sTxPending;

uint8_t sTxBuf[kApduBufferSize];
size_t sTxLen;
uint8_t sWireTxBuf[kApduBufferSize];
uint8_t sRxBuf[kApduBufferSize];
size_t sRxLen;

uint8_t sEcpFrame[kEcpFrameLen];
bool sEcpArmed;

/**
 * Workqueue handler that creates an Aliro session for the NFC connection.
 */
void CreateSessionWork(k_work *)
{
	Aliro::AliroStack::Instance().CreateSession(Aliro::ConnectionHandle::Nfc());
}

/**
 * Workqueue handler that delivers buffered NFC receive data (sRxBuf, sRxLen) to the Aliro stack's
 * session handler.
 */
void RxWork(k_work *)
{
	Aliro::AliroStack::Instance().HandleSessionData(Aliro::ConnectionHandle::Nfc(),
							{.mData = sRxBuf, .mLength = sRxLen});
}

/**
 * Workqueue handler that destroys the Aliro session for the NFC connection.
 */
void DestroySessionWork(k_work *)
{
	Aliro::AliroStack::Instance().DestroySession(Aliro::ConnectionHandle::Nfc());
}

K_WORK_DEFINE(sCreateSessionWork, CreateSessionWork);
K_WORK_DEFINE(sRxWork, RxWork);
K_WORK_DEFINE(sDestroySessionWork, DestroySessionWork);

/**
 * Populate the ECP (Emulated Card Proximity) Aliro frame with header, reader identifier, and CRC_A
 * checksum. Sets sEcpArmed to true and logs the frame for debug. Caller must call this before
 * BroadcastEcp.
 */
void ArmEcpFrame()
{
	std::memcpy(sEcpFrame, kAliroEcpHeader.data(), kAliroEcpHeader.size());

	Aliro::Identifier identifier{};
	if (DoorLock::ReaderStorage::IsIdentifierSet() &&
	    DoorLock::ReaderStorage::GetIdentifier(identifier) == ALIRO_NO_ERROR) {
		std::memcpy(sEcpFrame + kAliroEcpHeader.size(), identifier.data(), kReaderIdLen);
	} else {
		LOG_WRN("ECP: reader identifier not provisioned; emitting zero Reader Identifier");
		std::memset(sEcpFrame + kAliroEcpHeader.size(), 0, kReaderIdLen);
	}

	const uint16_t crc = pn532_crc_a(sEcpFrame, kEcpFrameLen - 2);
	sEcpFrame[kEcpFrameLen - 2] = static_cast<uint8_t>(crc & 0xFF);
	sEcpFrame[kEcpFrameLen - 1] = static_cast<uint8_t>(crc >> 8);
	sEcpArmed = true;

	LOG_HEXDUMP_DBG(sEcpFrame, kEcpFrameLen, "ECP Aliro frame armed:");
}

/* Fire-and-forget ECP beacon: CRC_A is precomputed in the frame, so the CIU
 * CRC engines are switched off around a raw InCommunicateThru. The expected
 * outcome is a chip-side timeout — nothing answers an ECP broadcast. */
void BroadcastEcp()
{
	if (!sEcpArmed) {
		return;
	}

	(void)pn532_set_rf_timeouts(&sChip, kAtrResTimeoutCode, kEcpTimeoutCode);
	(void)pn532_write_register(&sChip, PN532_REG_CIU_TX_MODE, 0x00);
	(void)pn532_write_register(&sChip, PN532_REG_CIU_RX_MODE, 0x00);

	const int rc = pn532_comm_thru(&sChip, sEcpFrame, kEcpFrameLen, nullptr, 0, nullptr, 100);
	if (rc != PN532_OK &&
	    !(rc == PN532_ERR_STATUS && sChip.last_status == PN532_STATUS_TIMEOUT)) {
		LOG_DBG("ECP broadcast anomaly: rc=%d status=0x%02x", rc, sChip.last_status);
	}

	/* Restore the CIU CRC engines and the APDU-length RF timeout. A dropped
	 * restore write silently leaves the chip with CRC off (the next
	 * InListPassiveTarget then quietly stops activating cards) or clips long APDU
	 * waits, with no error surfaced by the poll loop — warn so a wedged chip is
	 * diagnosable. */
	if (pn532_write_register(&sChip, PN532_REG_CIU_TX_MODE, 0x80) != PN532_OK ||
	    pn532_write_register(&sChip, PN532_REG_CIU_RX_MODE, 0x80) != PN532_OK ||
	    pn532_set_rf_timeouts(&sChip, kAtrResTimeoutCode, kExchangeTimeoutCode) != PN532_OK) {
		LOG_WRN("ECP: failed to restore CIU CRC / RF-timeout state; card activation may "
			"stall");
	}
}

/* Adapt one stack-level APDU to the PN532's local limits.  Intermediate 9000
 * responses belong to transport-created ENVELOPE fragments and are therefore
 * consumed here; the Aliro stack sees exactly one response to the APDU it sent. */
int ExchangeApdu(const pn532_target &target, size_t &wireTxLen)
{
	ultrawidelock_pn532_apdu_plan plan{};
	if (ultrawidelock_pn532_apdu_plan_init(sTxBuf, sTxLen, &plan) != 0) {
		return PN532_ERR_FRAME;
	}
	if (plan.adapted) {
		LOG_INF("PN532: adapting APDU INS=0x%02x tx=%zu data=%zu Le=%u "
			"(wire=%u, response=%u)",
			sTxBuf[1], sTxLen, plan.data_length, static_cast<unsigned int>(plan.le),
			ULTRAWIDELOCK_PN532_APDU_WIRE_MAX, ULTRAWIDELOCK_PN532_RESPONSE_DATA_MAX);
	}

	for (;;) {
		bool moreInternal = false;
		if (ultrawidelock_pn532_apdu_plan_next(&plan, sWireTxBuf, sizeof(sWireTxBuf), &wireTxLen,
					     &moreInternal) != 0) {
			return PN532_ERR_SPACE;
		}
		const int rc = pn532_in_data_exchange(&sChip, target.tg, sWireTxBuf, wireTxLen,
						      sRxBuf, sizeof(sRxBuf), &sRxLen,
						      CONFIG_ULTRAWIDELOCK_NFC_PN532_EXCHANGE_TIMEOUT_MS);
		if (rc != PN532_OK || sRxLen == 0) {
			return rc != PN532_OK ? rc : PN532_ERR_FRAME;
		}
		if (!moreInternal) {
			return PN532_OK;
		}
		if (sRxLen != 2 || sRxBuf[0] != 0x90 || sRxBuf[1] != 0x00) {
			/* Forward an early card-side error as the response to the original
			 * APDU; the blob/source stack remains responsible for policy. */
			LOG_WRN("PN532: internally chained ENVELOPE stopped with %zu-byte response",
				sRxLen);
			return PN532_OK;
		}
	}
}

/* One activated-device session: forward APDUs from Send() until the stack
 * terminates the session, polling stops, or an exchange fails. */
void RunSession(const pn532_target &target)
{
	atomic_clear(&sTxPending);
	atomic_clear(&sTerminateReq);
	k_sem_reset(&sWakeSem);
	atomic_set(&sTagActive, 1);

	LOG_INF("PN532: ISO-DEP device activated (SEL_RES 0x%02x, NFCID %u B)", target.sel_res,
		target.nfcid_len);
	(void)AliroWorkqueueSubmit(&sCreateSessionWork);

	while (atomic_get(&sStarted) && !atomic_get(&sTerminateReq)) {
		(void)k_sem_take(&sWakeSem, K_MSEC(100));
		if (!atomic_get(&sTxPending)) {
			continue;
		}

		size_t wireTxLen = 0;
		const int rc = ExchangeApdu(target, wireTxLen);
		atomic_clear(&sTxPending);

		if (rc != PN532_OK || sRxLen == 0) {
			/* status legend (PN532 UM0701 table 7-1): 0x01 = card timed out,
			 * 0x0e = the response overflowed the PN532's internal buffer (the
			 * negotiated APDU ceiling is too large for this chip), 0x13/0x23 =
			 * framing/parity from the card. */
			LOG_WRN("PN532: exchange failed (rc=%d status=0x%02x tx=%zu B "
				"wire_tx=%zu B rx=%zu B)",
				rc, sChip.last_status, sTxLen, wireTxLen, sRxLen);
			atomic_clear(&sTagActive);
			(void)AliroWorkqueueSubmit(&sDestroySessionWork);
			return;
		}
		(void)AliroWorkqueueSubmit(&sRxWork);
	}

	atomic_clear(&sTagActive);
	/* Reached only on a loop-condition exit (an exchange failure returns above
	 * having already destroyed the session). Either the stack terminated the
	 * session — sTerminateReq, and the contract says Terminate() must not call
	 * back into the stack — or the app stopped polling mid-session (Stop cleared
	 * sStarted), in which case the stack still believes the NFC session is live.
	 * Balance the CreateSession() posted at entry with a DestroySession() on that
	 * Stop path, else the next activation double-creates on ConnectionHandle::Nfc(). */
	if (!atomic_get(&sTerminateReq)) {
		(void)AliroWorkqueueSubmit(&sDestroySessionWork);
	}
}

/**
 * Run one RF poll cycle: enable field, broadcast ECP beacon, detect ISO-DEP targets for 400 ms, and
 * activate a session if exactly one ISO-DEP card is found. Disable field and sleep for
 * CONFIG_ULTRAWIDELOCK_NFC_PN532_POLL_PERIOD_MS before returning. Called by ThreadMain in a loop
 * when polling is active.
 */
void PollRound()
{
	pn532_target target;

	if (pn532_rf_field(&sChip, true) != PN532_OK) {
		LOG_WRN("PN532: RF field on failed");
		k_msleep(CONFIG_ULTRAWIDELOCK_NFC_PN532_POLL_PERIOD_MS);
		return;
	}
	k_msleep(2); /* field settle before the beacon */

	BroadcastEcp();

	const int rc = pn532_list_passive_target_106a(&sChip, &target, 400);
	if (rc == 1 && pn532_target_is_iso_dep(&target)) {
		RunSession(target);
	} else if (rc == 1) {
		LOG_DBG("PN532: non ISO-DEP card ignored (SEL_RES 0x%02x)", target.sel_res);
	} else if (rc != 0 && rc != PN532_ERR_STATUS) {
		LOG_DBG("PN532: poll error rc=%d", rc);
	}

	(void)pn532_in_release(&sChip);
	(void)pn532_rf_field(&sChip, false);
	k_msleep(CONFIG_ULTRAWIDELOCK_NFC_PN532_POLL_PERIOD_MS);
}

/**
 * Main loop of the PN532 polling thread: repeatedly run PollRound while sStarted is set, or park
 * the RF field and wait on sWakeSem with 500 ms timeout when stopped. Runs at preemptive
 * priority 7.
 */
void ThreadMain(void *, void *, void *)
{
	bool fieldParked = true;

	for (;;) {
		if (!atomic_get(&sStarted)) {
			if (!fieldParked) {
				(void)pn532_in_release(&sChip);
				(void)pn532_rf_field(&sChip, false);
				fieldParked = true;
			}
			(void)k_sem_take(&sWakeSem, K_MSEC(500));
			continue;
		}
		fieldParked = false;
		PollRound();
	}
}

} // namespace

namespace UltraWideLockNfc
{

/**
 * Initialize the PN532 NFC reader on SPI (spi1) with retries and RF configuration. Probes firmware
 * version up to 3 times with 10 ms delay between attempts. Creates and names the polling thread.
 * Returns ALIRO_NO_ERROR on success; logs specific wiring and configuration hints on failure.
 */
AliroError Init()
{
	if (sInitDone) {
		return ALIRO_NO_ERROR;
	}
	LOG_INF("PN532 bring-up on SPI (spi1: SCK P0.06, MOSI P0.07, MISO P0.25, CS P0.26, IRQ "
		"P1.08)");
	if (pn532_bus_init() != 0) {
		/* pn532_bus_spi logs the specific cause (bus not ready / IRQ GPIO). */
		return ALIRO_ERROR_INTERNAL;
	}

	pn532_init(&sChip, &pn532_bus_ops, pn532_bus_ctx());

	/* rc legend (pn532.h): -2 TIMEOUT = no ACK/ready (wiring, mode jumpers not
	 * on SPI, power); -3 FRAME = a reply arrived but was malformed (CRC/MISO
	 * noise); -1 IO = SPI transfer error. GetFirmwareVersion is the liveness
	 * probe; retry a few times so the first attempt can double as a wake. */
	uint8_t fw[4];
	int rc = PN532_ERR_TIMEOUT;

	for (int attempt = 0; attempt < 3; attempt++) {
		rc = pn532_get_firmware_version(&sChip, fw);
		if (rc == PN532_OK) {
			break;
		}
		LOG_WRN("PN532 probe attempt %d/3 failed (rc=%d, last_status=0x%02x)", attempt + 1,
			rc, sChip.last_status);
		k_msleep(10);
	}
	if (rc != PN532_OK) {
		LOG_ERR("PN532 not responding on SPI (rc=%d) — check the breakout's SPI-mode "
			"jumpers (Adafruit: SEL0 off/SEL1 on), 3V3 power, and MISO/MOSI/SCK/CS "
			"wiring",
			rc);
		return ALIRO_ERROR_INTERNAL;
	}
	LOG_INF("PN532 ready: IC 0x%02x firmware %u.%u", fw[0], fw[1], fw[2]);

	if ((rc = pn532_sam_configuration(&sChip)) != PN532_OK) {
		LOG_ERR("PN532 SAMConfiguration failed (rc=%d) after firmware probe succeeded", rc);
		return ALIRO_ERROR_INTERNAL;
	}

	if ((rc = pn532_set_retries(&sChip, 0x02, 0x01, 0x01)) != PN532_OK ||
	    (rc = pn532_set_rf_timeouts(&sChip, kAtrResTimeoutCode, kExchangeTimeoutCode)) !=
		    PN532_OK ||
	    (rc = pn532_rf_field(&sChip, false)) != PN532_OK) {
		LOG_ERR("PN532 RF configuration failed (rc=%d)", rc);
		return ALIRO_ERROR_INTERNAL;
	}

	sThreadId = k_thread_create(&sThreadData, sThreadStack, K_THREAD_STACK_SIZEOF(sThreadStack),
				    ThreadMain, nullptr, nullptr, nullptr, K_PRIO_PREEMPT(7), 0,
				    K_NO_WAIT);
	k_thread_name_set(sThreadId, "ultrawidelock_nfc_pn532");

	sInitDone = true;
	return ALIRO_NO_ERROR;
}

/**
 * Start NFC polling: arm the ECP frame, set sStarted, and wake the polling thread. Returns
 * ALIRO_ERROR_INTERNAL if not initialized; otherwise ALIRO_NO_ERROR and logs "PN532 polling
 * started".
 */
AliroError Start()
{
	if (!sInitDone) {
		return ALIRO_ERROR_INTERNAL;
	}
	ArmEcpFrame();
	atomic_set(&sStarted, 1);
	k_sem_give(&sWakeSem);
	LOG_INF("PN532 polling started");
	return ALIRO_NO_ERROR;
}

/**
 * Stop NFC polling by clearing sStarted and waking the polling thread. Returns ALIRO_NO_ERROR.
 */
AliroError Stop()
{
	atomic_clear(&sStarted);
	k_sem_give(&sWakeSem);
	return ALIRO_NO_ERROR;
}

/**
 * Queue outbound APDU data for transmission to the active NFC tag. Fails if no tag is active, data
 * pointer is null, length is zero or exceeds buffer, or a previous send is still pending. Sets
 * sTxPending and wakes the polling thread.
 */
AliroError Send(Aliro::Data data)
{
	if (!atomic_get(&sTagActive)) {
		LOG_WRN("NFC not activated, no data transfer possible");
		return ALIRO_INVALID_STATE;
	}
	if (data.mData == nullptr || data.mLength == 0 || data.mLength > sizeof(sTxBuf)) {
		return ALIRO_INVALID_ARGUMENT;
	}
	if (atomic_get(&sTxPending)) {
		LOG_ERR("PN532: send while previous exchange still pending");
		return ALIRO_INVALID_STATE;
	}

	std::memcpy(sTxBuf, data.mData, data.mLength);
	sTxLen = data.mLength;
	atomic_set(&sTxPending, 1);
	k_sem_give(&sWakeSem);
	return ALIRO_NO_ERROR;
}

/**
 * Request termination of the PN532 polling thread and cleanup. Sets sTerminateReq and wakes the
 * polling thread. Returns ALIRO_NO_ERROR.
 */
AliroError Terminate()
{
	atomic_set(&sTerminateReq, 1);
	k_sem_give(&sWakeSem);
	return ALIRO_NO_ERROR;
}

} // namespace UltraWideLockNfc
