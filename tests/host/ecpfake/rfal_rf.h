/* ecpfake: minimal ST RFAL RF surface for host-building nfc_prop_ecp.cpp.
 * rfalTransceiveBlockingTxRx is a recording double (ecpfake state below) — no
 * NFC field is involved; the suite asserts what would go over the air. */
#ifndef ECPFAKE_RFAL_RF_H
#define ECPFAKE_RFAL_RF_H

#include <stddef.h>
#include <stdint.h>

typedef uint16_t ReturnCode;

#define RFAL_ERR_NONE    ((ReturnCode)0)
#define RFAL_ERR_TIMEOUT ((ReturnCode)3)

#define RFAL_TXRX_FLAGS_CRC_TX_MANUAL 0x00000001u
#define RFAL_TXRX_FLAGS_CRC_RX_KEEP   0x00000002u

ReturnCode rfalTransceiveBlockingTxRx(uint8_t *txBuf, uint16_t txBufLen, uint8_t *rxBuf,
				      uint16_t rxBufLen, uint16_t *actLen, uint32_t flags,
				      uint32_t fwt);

/* Test-side recording state (defined in test_nfc_ecp.cpp). */
struct ecpfake_state {
	unsigned transceive_calls;
	uint8_t tx[64];
	uint16_t tx_len;
	uint32_t flags;
	uint32_t fwt;
	unsigned poller_init_calls;
	ReturnCode poller_init_ret;
	unsigned warns; /* LOG_WRN tally (unprovisioned reader id) */
	bool id_set;
	uint8_t id[8];
	int get_id_ret;
};
extern struct ecpfake_state ecpfake;

#endif /* ECPFAKE_RFAL_RF_H */
