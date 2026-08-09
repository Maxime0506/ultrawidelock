/* NXP PN532 host-protocol driver: frame codec and the command subset needed by
 * the Aliro reader transport. Bus-agnostic and OS-free — all I/O goes through
 * injected bus operations, so the whole layer compiles and runs in the host
 * test suite against a scripted fake bus.
 *
 * Protocol reference: NXP UM0701-02 (PN532 User Manual).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <woz_nfc/pn532_bus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes to read for an ACK frame (fixed 00 00 FF 00 FF 00 = 6, +2 slack for the
 * optional preamble alignment on SPI). The ACK read is kept tight on purpose: it
 * is immediately followed by the response frame, so a long over-read here would
 * clock the response out and lose it. Response reads may over-read freely — the
 * response is the last frame in a command cycle, nothing follows it. */
#define PN532_ACK_READ_LEN 8

/* Data bytes carried per InDataExchange chunk when chaining with the MI bit.
 * Normal host frames cap LEN (TFI..data) at 255; stay comfortably below. */
#define PN532_XFER_CHUNK 240

/* InDataExchange status byte: low 6 bits error code, bit 6 = more information. */
#define PN532_STATUS_ERR_MASK 0x3F
#define PN532_STATUS_MI       0x40
#define PN532_STATUS_OK       0x00
#define PN532_STATUS_TIMEOUT  0x01

/* Detail retained when PN532_ERR_FRAME is returned.  This stays separate from
 * the public return code so existing callers do not need special-case logic. */
enum pn532_frame_error {
	PN532_FRAME_ERROR_NONE = 0,
	PN532_FRAME_ERROR_START,
	PN532_FRAME_ERROR_SHORT_HEADER,
	PN532_FRAME_ERROR_NACK,
	PN532_FRAME_ERROR_EXTENDED_LCS,
	PN532_FRAME_ERROR_LCS,
	PN532_FRAME_ERROR_TRUNCATED,
	PN532_FRAME_ERROR_DCS,
	PN532_FRAME_ERROR_TFI,
	PN532_FRAME_ERROR_ACK_EXPECTED,
	PN532_FRAME_ERROR_RESPONSE_EXPECTED,
};

#define PN532_FRAME_DIAG_HEAD_SIZE 16

/* CIU register addresses used for raw (CRC-less) transmission. */
#define PN532_REG_CIU_TX_MODE 0x6302
#define PN532_REG_CIU_RX_MODE 0x6303

/**
 * PN532 NFC reader state: bus ops (read/write/wait_ready callbacks), timeouts (ack_ms,
 * response_ms), and diagnostic fields (last status, frame error, data length, checksum, head
 * bytes).
 */
struct pn532 {
	const struct pn532_bus_ops *ops;
	void *ctx;
	int ack_timeout_ms;
	int response_timeout_ms;
	/* Status byte of the last InDataExchange/InCommunicateThru response. */
	uint8_t last_status;
	/* Diagnostic context for the most recent host-frame parse. */
	enum pn532_frame_error last_frame_error;
	size_t last_frame_data_len;
	uint8_t last_frame_sum;
	uint8_t last_frame_head_len;
	uint8_t last_frame_head[PN532_FRAME_DIAG_HEAD_SIZE];
};

const char *pn532_frame_error_name(enum pn532_frame_error error);

/* Activated ISO14443A target as reported by InListPassiveTarget. */
struct pn532_target {
	uint8_t tg; /* logical target number to pass to InDataExchange */
	uint8_t sens_res[2];
	uint8_t sel_res;
	uint8_t nfcid_len;
	uint8_t nfcid[10];
	uint8_t ats_len;
	uint8_t ats[48];
};

/* SEL_RES bit 6: target speaks ISO-DEP (ISO14443-4). */
static inline bool pn532_target_is_iso_dep(const struct pn532_target *t)
{
	return (t->sel_res & 0x20) != 0;
}

void pn532_init(struct pn532 *p, const struct pn532_bus_ops *ops, void *ctx);

/* One full command transaction: write frame, consume ACK, read response.
 * resp receives the response payload after TFI and the echoed command byte.
 * A negative response_timeout_ms uses the default from struct pn532. */
int pn532_transact(struct pn532 *p, uint8_t cmd, const uint8_t *params, size_t params_len,
		   uint8_t *resp, size_t resp_cap, size_t *resp_len, int response_timeout_ms);

int pn532_get_firmware_version(struct pn532 *p, uint8_t out[4]);
/* SAMConfiguration, normal mode: mandatory once after power-up. */
int pn532_sam_configuration(struct pn532 *p);
int pn532_rf_field(struct pn532 *p, bool on);
/* RFConfiguration item 5: MxRtyATR, MxRtyPSL, MxRtyPassiveActivation. */
int pn532_set_retries(struct pn532 *p, uint8_t atr, uint8_t psl, uint8_t passive);
/* RFConfiguration item 2: fATR_RES_Timeout and fRetryTimeout codes
 * (timeout = 100 us * 2^(code - 1); 0x0B = 102 ms, 0x10 = 3.28 s). */
int pn532_set_rf_timeouts(struct pn532 *p, uint8_t atr_res_code, uint8_t retry_code);
int pn532_write_register(struct pn532 *p, uint16_t addr, uint8_t value);

/* Raw transceive via InCommunicateThru. The frame goes out exactly as given
 * (configure CIU_TX_MODE/CIU_RX_MODE first for CRC-less operation). Returns
 * PN532_ERR_STATUS with last_status = PN532_STATUS_TIMEOUT when nothing
 * answers — the expected outcome for a fire-and-forget ECP broadcast. */
int pn532_comm_thru(struct pn532 *p, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap,
		    size_t *rx_len, int timeout_ms);

/* Poll for one ISO14443A target at 106 kbps (InListPassiveTarget, BrTy 0).
 * Returns 1 with *out filled on activation, 0 when no target answered. */
int pn532_list_passive_target_106a(struct pn532 *p, struct pn532_target *out, int timeout_ms);

/* One APDU round trip against an activated target. Handles ISO-DEP chaining in
 * both directions: requests larger than PN532_XFER_CHUNK go out via the MI
 * bit, and chained responses are reassembled into rx. */
int pn532_in_data_exchange(struct pn532 *p, uint8_t tg, const uint8_t *tx, size_t tx_len,
			   uint8_t *rx, size_t rx_cap, size_t *rx_len, int timeout_ms);

/* Release all activated targets (InRelease 0). */
int pn532_in_release(struct pn532 *p);

/* ISO/IEC 14443-3 CRC_A (init 0x6363), appended little-endian on the wire. */
uint16_t pn532_crc_a(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
