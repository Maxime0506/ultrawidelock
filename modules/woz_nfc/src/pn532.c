/* PN532 host-protocol driver. See pn532.h. OS-free: no Zephyr headers, no
 * allocation, no sleeping — waiting is delegated to the bus wait_ready op. */

#include "pn532.h"

#include <string.h>

#define PN532_TFI_HOST_TO_CHIP 0xD4
#define PN532_TFI_CHIP_TO_HOST 0xD5
#define PN532_TFI_ERROR        0x7F

#define PN532_CMD_GET_FIRMWARE_VERSION   0x02
#define PN532_CMD_WRITE_REGISTER         0x08
#define PN532_CMD_SAM_CONFIGURATION      0x14
#define PN532_CMD_RF_CONFIGURATION       0x32
#define PN532_CMD_IN_DATA_EXCHANGE       0x40
#define PN532_CMD_IN_COMMUNICATE_THRU    0x42
#define PN532_CMD_IN_LIST_PASSIVE_TARGET 0x4A
#define PN532_CMD_IN_RELEASE             0x52

#define PN532_DEFAULT_ACK_TIMEOUT_MS      100
#define PN532_DEFAULT_RESPONSE_TIMEOUT_MS 1000

void pn532_init(struct pn532 *p, const struct pn532_bus_ops *ops, void *ctx)
{
	memset(p, 0, sizeof(*p));
	p->ops = ops;
	p->ctx = ctx;
	p->ack_timeout_ms = PN532_DEFAULT_ACK_TIMEOUT_MS;
	p->response_timeout_ms = PN532_DEFAULT_RESPONSE_TIMEOUT_MS;
}

/* Build a normal information frame around TFI 0xD4 + cmd + params. */
static size_t build_frame(uint8_t cmd, const uint8_t *params, size_t params_len, uint8_t *out)
{
	const uint8_t len = (uint8_t)(2 + params_len); /* TFI + PD0 + params */
	uint8_t dcs = (uint8_t)(PN532_TFI_HOST_TO_CHIP + cmd);
	size_t at = 0;

	out[at++] = 0x00; /* preamble */
	out[at++] = 0x00; /* start code */
	out[at++] = 0xFF;
	out[at++] = len;
	out[at++] = (uint8_t)(0x100 - len); /* LCS: LEN + LCS == 0 mod 256 */
	out[at++] = PN532_TFI_HOST_TO_CHIP;
	out[at++] = cmd;
	for (size_t i = 0; i < params_len; i++) {
		out[at++] = params[i];
		dcs = (uint8_t)(dcs + params[i]);
	}
	out[at++] = (uint8_t)(0x100 - dcs); /* DCS: sum(TFI..data) + DCS == 0 */
	out[at++] = 0x00;                   /* postamble */
	return at;
}

/* Locate the 00 FF start code, tolerating leading preamble/idle bytes. */
static int find_start(const uint8_t *buf, size_t len, size_t *start)
{
	for (size_t i = 0; i + 1 < len; i++) {
		if (buf[i] == 0x00 && buf[i + 1] == 0xFF) {
			*start = i + 2;
			return PN532_OK;
		}
		if (buf[i] != 0x00) {
			break;
		}
	}
	return PN532_ERR_FRAME;
}

const char *pn532_frame_error_name(enum pn532_frame_error error)
{
	switch (error) {
	case PN532_FRAME_ERROR_NONE:
		return "none";
	case PN532_FRAME_ERROR_START:
		return "start-code";
	case PN532_FRAME_ERROR_SHORT_HEADER:
		return "short-header";
	case PN532_FRAME_ERROR_NACK:
		return "nack";
	case PN532_FRAME_ERROR_EXTENDED_LCS:
		return "extended-lcs";
	case PN532_FRAME_ERROR_LCS:
		return "lcs";
	case PN532_FRAME_ERROR_TRUNCATED:
		return "truncated";
	case PN532_FRAME_ERROR_DCS:
		return "dcs";
	case PN532_FRAME_ERROR_TFI:
		return "tfi";
	case PN532_FRAME_ERROR_ACK_EXPECTED:
		return "ack-expected";
	case PN532_FRAME_ERROR_RESPONSE_EXPECTED:
		return "response-expected";
	default:
		return "unknown";
	}
}

static int frame_error(struct pn532 *p, enum pn532_frame_error error)
{
	p->last_frame_error = error;
	return PN532_ERR_FRAME;
}

/* Parse one chip-to-host frame. On success *payload points at PD0 (the byte
 * after TFI) inside buf and *payload_len counts PD0 onward. is_ack reports a
 * bare ACK frame (no payload). */
static int parse_frame(struct pn532 *p, const uint8_t *buf, size_t len, const uint8_t **payload,
		       size_t *payload_len, bool *is_ack)
{
	size_t at = 0;
	int rc = find_start(buf, len, &at);

	p->last_frame_error = PN532_FRAME_ERROR_NONE;
	p->last_frame_data_len = 0;
	p->last_frame_sum = 0;
	p->last_frame_head_len =
		(uint8_t)(len < PN532_FRAME_DIAG_HEAD_SIZE ? len : PN532_FRAME_DIAG_HEAD_SIZE);
	memcpy(p->last_frame_head, buf, p->last_frame_head_len);

	if (rc != PN532_OK) {
		return frame_error(p, PN532_FRAME_ERROR_START);
	}
	if (len - at < 2) {
		return frame_error(p, PN532_FRAME_ERROR_SHORT_HEADER);
	}

	*is_ack = false;

	/* ACK (00 FF) / NACK (FF 00) frames have no length field. */
	if (buf[at] == 0x00 && buf[at + 1] == 0xFF) {
		*is_ack = true;
		*payload = NULL;
		*payload_len = 0;
		return PN532_OK;
	}
	if (buf[at] == 0xFF && buf[at + 1] == 0x00) {
		return frame_error(p, PN532_FRAME_ERROR_NACK);
	}

	size_t data_len;
	if (buf[at] == 0xFF && buf[at + 1] == 0xFF) {
		/* Extended frame: LENM LENL LCS. */
		if (len - at < 5) {
			return frame_error(p, PN532_FRAME_ERROR_SHORT_HEADER);
		}
		data_len = ((size_t)buf[at + 2] << 8) | buf[at + 3];
		p->last_frame_data_len = data_len;
		if ((uint8_t)(buf[at + 2] + buf[at + 3] + buf[at + 4]) != 0) {
			return frame_error(p, PN532_FRAME_ERROR_EXTENDED_LCS);
		}
		at += 5;
	} else {
		data_len = buf[at];
		p->last_frame_data_len = data_len;
		if ((uint8_t)(buf[at] + buf[at + 1]) != 0) {
			return frame_error(p, PN532_FRAME_ERROR_LCS);
		}
		at += 2;
	}

	if (data_len < 1 || len - at < data_len + 1) {
		return frame_error(p, PN532_FRAME_ERROR_TRUNCATED);
	}

	uint8_t sum = 0;
	for (size_t i = 0; i < data_len + 1; i++) { /* TFI..data + DCS */
		sum = (uint8_t)(sum + buf[at + i]);
	}
	p->last_frame_sum = sum;
	if (sum != 0) {
		return frame_error(p, PN532_FRAME_ERROR_DCS);
	}

	if (buf[at] == PN532_TFI_ERROR) {
		return PN532_ERR_APP;
	}
	if (buf[at] != PN532_TFI_CHIP_TO_HOST) {
		return frame_error(p, PN532_FRAME_ERROR_TFI);
	}

	*payload = &buf[at + 1];
	*payload_len = data_len - 1;
	return PN532_OK;
}

/* read_len bounds how many bytes to clock out of the chip: PN532_ACK_READ_LEN
 * for the ACK (kept tight, see pn532.h) and PN532_FRAME_BUF_SIZE for a response
 * (over-read is fine and lets the transport stay length-agnostic). */
static int read_frame(struct pn532 *p, uint8_t *buf, size_t read_len, const uint8_t **payload,
		      size_t *payload_len, bool *is_ack, int timeout_ms)
{
	int rc = p->ops->wait_ready(p->ctx, timeout_ms);

	if (rc != PN532_OK) {
		return rc;
	}
	rc = p->ops->read(p->ctx, buf, read_len);
	if (rc != PN532_OK) {
		return PN532_ERR_IO;
	}
	return parse_frame(p, buf, read_len, payload, payload_len, is_ack);
}

int pn532_transact(struct pn532 *p, uint8_t cmd, const uint8_t *params, size_t params_len,
		   uint8_t *resp, size_t resp_cap, size_t *resp_len, int response_timeout_ms)
{
	uint8_t frame[PN532_FRAME_BUF_SIZE];
	const uint8_t *payload;
	size_t payload_len;
	bool is_ack;
	int rc;

	if (params_len > (size_t)(253)) {
		return PN532_ERR_SPACE; /* normal frames only; callers chunk */
	}

	const size_t frame_len = build_frame(cmd, params, params_len, frame);

	rc = p->ops->write(p->ctx, frame, frame_len);
	if (rc != PN532_OK) {
		return PN532_ERR_IO;
	}

	rc = read_frame(p, frame, PN532_ACK_READ_LEN, &payload, &payload_len, &is_ack,
			p->ack_timeout_ms);
	if (rc != PN532_OK) {
		return rc;
	}
	if (!is_ack) {
		return frame_error(p, PN532_FRAME_ERROR_ACK_EXPECTED);
	}

	rc = read_frame(p, frame, PN532_FRAME_BUF_SIZE, &payload, &payload_len, &is_ack,
			response_timeout_ms >= 0 ? response_timeout_ms : p->response_timeout_ms);
	if (rc != PN532_OK) {
		return rc;
	}
	if (is_ack || payload_len < 1 || payload[0] != (uint8_t)(cmd + 1)) {
		return frame_error(p, PN532_FRAME_ERROR_RESPONSE_EXPECTED);
	}

	payload++; /* skip echoed response code */
	payload_len--;
	if (resp_len != NULL) {
		*resp_len = payload_len;
	}
	if (payload_len > 0) {
		if (resp == NULL || payload_len > resp_cap) {
			return PN532_ERR_SPACE;
		}
		memcpy(resp, payload, payload_len);
	}
	return PN532_OK;
}

int pn532_get_firmware_version(struct pn532 *p, uint8_t out[4])
{
	size_t len = 0;
	int rc = pn532_transact(p, PN532_CMD_GET_FIRMWARE_VERSION, NULL, 0, out, 4, &len, -1);

	if (rc != PN532_OK) {
		return rc;
	}
	return len == 4 ? PN532_OK : PN532_ERR_FRAME;
}

int pn532_sam_configuration(struct pn532 *p)
{
	/* Normal mode, default timeout field, IRQ pad enabled. */
	static const uint8_t params[] = {0x01, 0x14, 0x01};

	return pn532_transact(p, PN532_CMD_SAM_CONFIGURATION, params, sizeof(params), NULL, 0, NULL,
			      -1);
}

int pn532_rf_field(struct pn532 *p, bool on)
{
	const uint8_t params[] = {0x01, on ? 0x01 : 0x00};

	return pn532_transact(p, PN532_CMD_RF_CONFIGURATION, params, sizeof(params), NULL, 0, NULL,
			      -1);
}

int pn532_set_retries(struct pn532 *p, uint8_t atr, uint8_t psl, uint8_t passive)
{
	const uint8_t params[] = {0x05, atr, psl, passive};

	return pn532_transact(p, PN532_CMD_RF_CONFIGURATION, params, sizeof(params), NULL, 0, NULL,
			      -1);
}

int pn532_set_rf_timeouts(struct pn532 *p, uint8_t atr_res_code, uint8_t retry_code)
{
	const uint8_t params[] = {0x02, 0x00, atr_res_code, retry_code};

	return pn532_transact(p, PN532_CMD_RF_CONFIGURATION, params, sizeof(params), NULL, 0, NULL,
			      -1);
}

int pn532_write_register(struct pn532 *p, uint16_t addr, uint8_t value)
{
	const uint8_t params[] = {(uint8_t)(addr >> 8), (uint8_t)addr, value};

	return pn532_transact(p, PN532_CMD_WRITE_REGISTER, params, sizeof(params), NULL, 0, NULL,
			      -1);
}

int pn532_comm_thru(struct pn532 *p, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap,
		    size_t *rx_len, int timeout_ms)
{
	uint8_t resp[PN532_FRAME_BUF_SIZE];
	size_t resp_len = 0;
	int rc = pn532_transact(p, PN532_CMD_IN_COMMUNICATE_THRU, tx, tx_len, resp, sizeof(resp),
				&resp_len, timeout_ms);

	if (rc != PN532_OK) {
		return rc;
	}
	if (resp_len < 1) {
		return PN532_ERR_FRAME;
	}
	p->last_status = resp[0];
	if ((resp[0] & PN532_STATUS_ERR_MASK) != PN532_STATUS_OK) {
		return PN532_ERR_STATUS;
	}
	if (rx_len != NULL) {
		*rx_len = resp_len - 1;
	}
	if (resp_len - 1 > 0) {
		if (rx == NULL || resp_len - 1 > rx_cap) {
			return PN532_ERR_SPACE;
		}
		memcpy(rx, &resp[1], resp_len - 1);
	}
	return PN532_OK;
}

int pn532_list_passive_target_106a(struct pn532 *p, struct pn532_target *out, int timeout_ms)
{
	/* MaxTg = 1 target, BrTy = 0x00: 106 kbps ISO14443 type A. */
	static const uint8_t params[] = {0x01, 0x00};
	uint8_t resp[PN532_FRAME_BUF_SIZE];
	size_t resp_len = 0;
	int rc = pn532_transact(p, PN532_CMD_IN_LIST_PASSIVE_TARGET, params, sizeof(params), resp,
				sizeof(resp), &resp_len, timeout_ms);

	if (rc != PN532_OK) {
		return rc;
	}
	if (resp_len < 1) {
		return PN532_ERR_FRAME;
	}
	if (resp[0] == 0) {
		return 0; /* no target in field */
	}

	/* NbTg, Tg, SENS_RES[2], SEL_RES, NFCIDLength, NFCID..., then for an
	 * ISO-DEP target the ATS with its TL length byte first. */
	size_t at = 1;

	memset(out, 0, sizeof(*out));
	if (resp_len - at < 5) {
		return PN532_ERR_FRAME;
	}
	out->tg = resp[at++];
	out->sens_res[0] = resp[at++];
	out->sens_res[1] = resp[at++];
	out->sel_res = resp[at++];
	out->nfcid_len = resp[at++];
	if (out->nfcid_len > sizeof(out->nfcid) || resp_len - at < out->nfcid_len) {
		return PN532_ERR_FRAME;
	}
	memcpy(out->nfcid, &resp[at], out->nfcid_len);
	at += out->nfcid_len;

	if (resp_len > at) {
		size_t ats_len = resp[at]; /* TL counts itself */

		if (ats_len < 1 || ats_len > sizeof(out->ats) || resp_len - at < ats_len) {
			return PN532_ERR_FRAME;
		}
		out->ats_len = (uint8_t)ats_len;
		memcpy(out->ats, &resp[at], ats_len);
	}
	return 1;
}

int pn532_in_data_exchange(struct pn532 *p, uint8_t tg, const uint8_t *tx, size_t tx_len,
			   uint8_t *rx, size_t rx_cap, size_t *rx_len, int timeout_ms)
{
	uint8_t params[1 + PN532_XFER_CHUNK];
	uint8_t resp[PN532_FRAME_BUF_SIZE];
	size_t sent = 0;
	size_t received = 0;
	size_t resp_len = 0;
	int rc;

	if (rx_len != NULL) {
		*rx_len = 0;
	}

	/* Send, chaining with the MI bit while more request bytes remain. */
	do {
		const size_t chunk =
			(tx_len - sent) > PN532_XFER_CHUNK ? PN532_XFER_CHUNK : (tx_len - sent);
		const bool more = (sent + chunk) < tx_len;

		params[0] = more ? (uint8_t)(tg | PN532_STATUS_MI) : tg;
		memcpy(&params[1], &tx[sent], chunk);
		rc = pn532_transact(p, PN532_CMD_IN_DATA_EXCHANGE, params, 1 + chunk, resp,
				    sizeof(resp), &resp_len, timeout_ms);
		if (rc != PN532_OK) {
			return rc;
		}
		if (resp_len < 1) {
			return PN532_ERR_FRAME;
		}
		p->last_status = resp[0];
		if ((resp[0] & PN532_STATUS_ERR_MASK) != PN532_STATUS_OK) {
			return PN532_ERR_STATUS;
		}
		sent += chunk;
	} while (sent < tx_len);

	/* Collect the response, following the MI bit for chained answers. */
	for (;;) {
		const size_t data_len = resp_len - 1;

		if (data_len > 0) {
			if (rx == NULL || received + data_len > rx_cap) {
				return PN532_ERR_SPACE;
			}
			memcpy(&rx[received], &resp[1], data_len);
			received += data_len;
		}
		if ((p->last_status & PN532_STATUS_MI) == 0) {
			break;
		}
		params[0] = tg;
		rc = pn532_transact(p, PN532_CMD_IN_DATA_EXCHANGE, params, 1, resp, sizeof(resp),
				    &resp_len, timeout_ms);
		if (rc != PN532_OK) {
			return rc;
		}
		if (resp_len < 1) {
			return PN532_ERR_FRAME;
		}
		p->last_status = resp[0];
		if ((resp[0] & PN532_STATUS_ERR_MASK) != PN532_STATUS_OK) {
			return PN532_ERR_STATUS;
		}
	}

	if (rx_len != NULL) {
		*rx_len = received;
	}
	return PN532_OK;
}

int pn532_in_release(struct pn532 *p)
{
	static const uint8_t params[] = {0x00}; /* all targets */
	uint8_t status = 0;
	size_t len = 0;
	int rc = pn532_transact(p, PN532_CMD_IN_RELEASE, params, sizeof(params), &status,
				sizeof(status), &len, -1);

	if (rc != PN532_OK) {
		return rc;
	}
	return PN532_OK;
}

uint16_t pn532_crc_a(const uint8_t *data, size_t len)
{
	uint16_t crc = 0x6363;

	for (size_t i = 0; i < len; i++) {
		uint8_t b = (uint8_t)(data[i] ^ (crc & 0xFF));

		b = (uint8_t)(b ^ (b << 4));
		crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^
				 ((uint16_t)b >> 4));
	}
	return crc;
}
