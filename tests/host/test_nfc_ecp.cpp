/**
 * @file test_nfc_ecp.cpp — the Aliro NFC ECP emitter
 * (modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp) on host, over fake RFAL /
 * reader-storage headers (tests/host/ecpfake/). Standalone g++ binary: the
 * source is C++ and its Nordic/ST dependencies are faked, so this pins the
 * frame layout (header || reader id || CRC_A), the CRC_A math against an
 * independent bit-reflected reference, and the armed/fire-and-forget beacon
 * gating — never a real NFC field.
 */
#include <cstdio>
#include <cstring>

#include <nfc_prop/nfc_prop.h>
#include <reader_storage/reader.h>
#include <rfal_nfca.h>

extern "C" {
#include "test.h"
}

struct ecpfake_state ecpfake;

/* ── fake RFAL + storage backends (declared in the ecpfake headers) ────────── */
ReturnCode rfalNfcaPollerInitialize(void)
{
	ecpfake.poller_init_calls++;
	return ecpfake.poller_init_ret;
}

ReturnCode rfalTransceiveBlockingTxRx(uint8_t *txBuf, uint16_t txBufLen, uint8_t *rxBuf,
				      uint16_t rxBufLen, uint16_t *actLen, uint32_t flags,
				      uint32_t fwt)
{
	(void)rxBuf;
	(void)rxBufLen;
	ecpfake.transceive_calls++;
	ecpfake.tx_len = txBufLen <= sizeof(ecpfake.tx) ? txBufLen : sizeof(ecpfake.tx);
	std::memcpy(ecpfake.tx, txBuf, ecpfake.tx_len);
	ecpfake.flags = flags;
	ecpfake.fwt = fwt;
	if (actLen != nullptr) {
		*actLen = 0;
	}
	return RFAL_ERR_NONE;
}

bool DoorLock::ReaderStorage::IsIdentifierSet(void)
{
	return ecpfake.id_set;
}

int DoorLock::ReaderStorage::GetIdentifier(Aliro::Identifier &out)
{
	std::memcpy(out.data(), ecpfake.id, sizeof(ecpfake.id));
	return ecpfake.get_id_ret;
}

/* ── independent CRC_A reference: bit-reflected 0x8408, init 0x6363 ────────── */
static uint16_t crc_a_ref(const uint8_t *data, size_t n)
{
	uint16_t crc = 0x6363;

	for (size_t i = 0; i < n; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u)
					 : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

static const uint8_t kHeader[8] = {0x6A, 0x02, 0xCB, 0x02, 0x06, 0x20, 0x42, 0x20};

int main(void)
{
	const rfalNfcPropCallbacks *cb = NfcPropGetCallbacks();

	t_group("callback table wiring");
	T_OK("table returned", cb != nullptr);
	T_OK("init wired", cb->rfalNfcpPollerInitialize != nullptr);
	T_OK("detect wired", cb->rfalNfcpPollerTechnologyDetection != nullptr);
	T_OK("unused slots null", cb->rfalNfcpPollerStartCollisionResolution == nullptr &&
					  cb->rfalNfcpStartActivation == nullptr);
	std::memset(&ecpfake, 0, sizeof(ecpfake));
	T_EQ("PropInit passes through", cb->rfalNfcpPollerInitialize(), RFAL_ERR_NONE);
	T_EQ("poller initialized", (long)ecpfake.poller_init_calls, 1L);

	t_group("unarmed: no beacon");
	T_EQ("detect reports timeout", cb->rfalNfcpPollerTechnologyDetection(),
	     RFAL_ERR_TIMEOUT);
	T_EQ("nothing transmitted", (long)ecpfake.transceive_calls, 0L);

	t_group("arm unprovisioned: zero reader id + warning");
	ecpfake.id_set = false;
	NfcPropInit();
	T_EQ("warned once", (long)ecpfake.warns, 1L);
	T_EQ("beacon fires", cb->rfalNfcpPollerTechnologyDetection(), RFAL_ERR_TIMEOUT);
	T_EQ("one transceive", (long)ecpfake.transceive_calls, 1L);
	T_EQ("18-byte ECP frame", (long)ecpfake.tx_len, 18L);
	T_OK("aliro ecp header", std::memcmp(ecpfake.tx, kHeader, 8) == 0);
	{
		uint8_t zeros[8] = {0};

		T_OK("zero reader id", std::memcmp(ecpfake.tx + 8, zeros, 8) == 0);
	}
	{
		uint16_t crc = crc_a_ref(ecpfake.tx, 16);

		T_OK("crc_a lo", ecpfake.tx[16] == (uint8_t)(crc & 0xFF));
		T_OK("crc_a hi", ecpfake.tx[17] == (uint8_t)(crc >> 8));
	}
	T_EQ("manual-CRC flags", (long)ecpfake.flags,
	     (long)(RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP));
	T_EQ("FDT min", (long)ecpfake.fwt, (long)RFAL_NFCA_FDTMIN);

	t_group("arm provisioned: reader id carried");
	static const uint8_t rid[8] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18};

	ecpfake.id_set = true;
	std::memcpy(ecpfake.id, rid, sizeof(rid));
	ecpfake.warns = 0;
	NfcPropInit();
	T_EQ("no warning", (long)ecpfake.warns, 0L);
	(void)cb->rfalNfcpPollerTechnologyDetection();
	T_OK("reader id in frame", std::memcmp(ecpfake.tx + 8, rid, 8) == 0);
	{
		uint16_t crc = crc_a_ref(ecpfake.tx, 16);

		T_OK("crc follows the id", ecpfake.tx[16] == (uint8_t)(crc & 0xFF) &&
						   ecpfake.tx[17] == (uint8_t)(crc >> 8));
	}

	t_group("storage read failure: falls back to zero id");
	ecpfake.get_id_ret = -1; /* != ALIRO_NO_ERROR */
	ecpfake.warns = 0;
	NfcPropInit();
	T_EQ("warned", (long)ecpfake.warns, 1L);
	(void)cb->rfalNfcpPollerTechnologyDetection();
	{
		uint8_t zeros[8] = {0};

		T_OK("zeroed again", std::memcmp(ecpfake.tx + 8, zeros, 8) == 0);
	}

	if (t_fail > 0) {
		printf("  nfc-ecp: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	printf("  nfc-ecp: PASS (%d checks — fake RFAL, no NFC field)\n", t_pass);
	return 0;
}
