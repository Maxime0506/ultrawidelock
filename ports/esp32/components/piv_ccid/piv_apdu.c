#include "piv_apdu.h"

#include <string.h>

#define SW_SUCCESS 0x9000u
#define SW_BYTES_REMAINING 0x6100u
#define SW_WRONG_LENGTH 0x6700u
#define SW_SECURITY_NOT_SATISFIED 0x6982u
#define SW_AUTH_BLOCKED 0x6983u
#define SW_APP_NOT_SELECTED 0x6999u
#define SW_BAD_DATA 0x6a80u
#define SW_FILE_NOT_FOUND 0x6a82u
#define SW_BAD_PARAMETERS 0x6a86u
#define SW_REFERENCE_NOT_FOUND 0x6a88u
#define SW_INS_NOT_SUPPORTED 0x6d00u
#define SW_CLA_NOT_SUPPORTED 0x6e00u

#define PIV_KEY_REF_PIN 0x80u
#define PIV_ALG_ECC_P256 0x11u

static const uint8_t s_piv_aid[] = {
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
};

static const uint8_t s_piv_aid_truncated[] = {
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00,
};

/*
 * NIST SP 800-73, application property template. The AC template advertises
 * only ECC P-256, the one algorithm this token actually implements.
 */
static const uint8_t s_piv_application_properties[] = {
	0x61, 0x1d,
	0x4f, 0x0b,
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
	0x79, 0x07,
	0x4f, 0x05, 0xa0, 0x00, 0x00, 0x03, 0x08,
	0xac, 0x05, 0x80, 0x01, PIV_ALG_ECC_P256, 0x06, 0x00,
};

/* PIV application PIN only, no Global PIN, OCC, or VCI. */
static const uint8_t s_discovery_object[] = {
	0x7e, 0x12,
	0x4f, 0x0b,
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
	0x5f, 0x2f, 0x02, 0x40, 0x00,
};

/*
 * PIV CCC. SP 800-73 permits zero-length mandatory elements other than F5;
 * the registered PIV data model number is 0x10.
 */
static const uint8_t s_ccc_value[] = {
	0xf0, 0x00, 0xf1, 0x00, 0xf2, 0x00, 0xf3, 0x00, 0xf4, 0x00,
	0xf5, 0x01, 0x10, 0xf6, 0x00, 0xf7, 0x00, 0xfa, 0x00,
	0xfb, 0x00, 0xfc, 0x00, 0xfd, 0x00, 0xfe, 0x00,
};

/* Valid all-zero FASC-N encoding used only as a non-identifying placeholder. */
static const uint8_t s_zero_fascn[] = {
	0xd4, 0xe7, 0x39, 0xda, 0x73, 0x9c, 0xed, 0x39, 0xce,
	0x73, 0x9d, 0x83, 0x68, 0x58, 0x21, 0x08, 0x42, 0x10,
	0x84, 0x21, 0xc8, 0x42, 0x10, 0xc3, 0xeb,
};

/**
 * A parsed ISO 7816 command APDU: class, instruction, parameters, data payload, and optional Le
 * (expected response length).
 */
struct command_apdu {
	uint8_t cla;
	uint8_t ins;
	uint8_t p1;
	uint8_t p2;
	const uint8_t *data;
	size_t data_len;
	bool le_present;
	size_t le;
};

/**
 * Write data and a status word to the response buffer and record its length, returning 0 on success
 * or -1 if the buffer is too small.
 */
static int finish(const uint8_t *data, size_t data_len, uint16_t sw,
		  uint8_t *response, size_t response_cap, size_t *response_len)
{
	if (response == NULL || response_len == NULL ||
	    data_len > response_cap || response_cap - data_len < 2u) {
		return -1;
	}
	if (data_len != 0u) {
		memcpy(response, data, data_len);
	}
	response[data_len] = (uint8_t)(sw >> 8);
	response[data_len + 1u] = (uint8_t)sw;
	*response_len = data_len + 2u;
	return 0;
}

/**
 * Parse an ISO 7816 command APDU from bytes: extract CLA, INS, P1, P2, data length, data, and
 * optional Le. Return 0 on success or -1 if the length is invalid.
 */
static int parse_command(const uint8_t *command, size_t command_len,
			 struct command_apdu *apdu)
{
	if (command == NULL || apdu == NULL || command_len < 4u) {
		return -1;
	}
	memset(apdu, 0, sizeof(*apdu));
	apdu->cla = command[0];
	apdu->ins = command[1];
	apdu->p1 = command[2];
	apdu->p2 = command[3];

	if (command_len == 4u) {
		return 0;
	}
	if (command_len == 5u) {
		apdu->le_present = true;
		apdu->le = command[4] == 0u ? 256u : command[4];
		return 0;
	}

	apdu->data_len = command[4];
	if (command_len != 5u + apdu->data_len &&
	    command_len != 6u + apdu->data_len) {
		return -1;
	}
	apdu->data = command + 5u;
	if (command_len == 6u + apdu->data_len) {
		apdu->le_present = true;
		apdu->le = command[5u + apdu->data_len] == 0u ?
			   256u : command[5u + apdu->data_len];
	}
	return 0;
}

/**
 * Return the number of bytes needed to encode a BER length: 1 if less than 128, 2 if up to 255, or
 * 3 if up to 65535.
 */
static size_t ber_length_bytes(size_t len)
{
	return len < 0x80u ? 1u : (len <= 0xffu ? 2u : 3u);
}

/**
 * Encode a BER length into the output buffer: 1 byte for lengths under 128, 2 for up to 255, 3 for
 * up to 65535. Return 0 on success or -1 if the buffer is too small.
 */
static int put_ber_length(uint8_t *out, size_t cap, size_t len, size_t *written)
{
	if (out == NULL || written == NULL) {
		return -1;
	}
	if (len < 0x80u) {
		if (cap < 1u) {
			return -1;
		}
		out[0] = (uint8_t)len;
		*written = 1u;
		return 0;
	}
	if (len <= 0xffu) {
		if (cap < 2u) {
			return -1;
		}
		out[0] = 0x81u;
		out[1] = (uint8_t)len;
		*written = 2u;
		return 0;
	}
	if (len <= 0xffffu && cap >= 3u) {
		out[0] = 0x82u;
		out[1] = (uint8_t)(len >> 8);
		out[2] = (uint8_t)len;
		*written = 3u;
		return 0;
	}
	return -1;
}

static int wrap_tlv(uint8_t tag, const uint8_t *value, size_t value_len,
		    uint8_t *out, size_t cap, size_t *out_len)
{
	size_t length_len;

	if (out == NULL || out_len == NULL ||
	    cap < 1u + ber_length_bytes(value_len) ||
	    value_len > cap - 1u - ber_length_bytes(value_len)) {
		return -1;
	}
	out[0] = tag;
	if (put_ber_length(out + 1u, cap - 1u, value_len, &length_len) != 0) {
		return -1;
	}
	if (value_len != 0u) {
		memcpy(out + 1u + length_len, value, value_len);
	}
	*out_len = 1u + length_len + value_len;
	return 0;
}

/**
 * Emit a chunk of a pending response, up to the requested size, followed by a status word
 * indicating how many bytes remain: SW_SUCCESS if done, or SW_BYTES_REMAINING with the count.
 */
static int emit_pending(struct piv_apdu *piv, size_t requested,
			uint8_t *response, size_t response_cap,
			size_t *response_len)
{
	size_t remaining;
	size_t chunk;
	uint16_t sw;

	if (piv == NULL || piv->pending_offset > piv->pending_len) {
		return -1;
	}
	remaining = piv->pending_len - piv->pending_offset;
	chunk = remaining < requested ? remaining : requested;
	if (chunk > response_cap || response_cap - chunk < 2u) {
		return -1;
	}
	if (chunk != 0u) {
		memcpy(response, piv->pending + piv->pending_offset, chunk);
		piv->pending_offset += chunk;
	}
	remaining = piv->pending_len - piv->pending_offset;
	if (remaining == 0u) {
		sw = SW_SUCCESS;
		piv->pending_len = 0u;
		piv->pending_offset = 0u;
	} else {
		sw = SW_BYTES_REMAINING |
		     (remaining >= 256u ? 0u : (uint16_t)remaining);
	}
	response[chunk] = (uint8_t)(sw >> 8);
	response[chunk + 1u] = (uint8_t)sw;
	*response_len = chunk + 2u;
	return 0;
}

static int send_data(struct piv_apdu *piv, const uint8_t *data, size_t data_len,
		     size_t requested, uint8_t *response, size_t response_cap,
		     size_t *response_len)
{
	if (piv == NULL || data_len > sizeof(piv->pending)) {
		return -1;
	}
	if (data_len != 0u) {
		memcpy(piv->pending, data, data_len);
	}
	piv->pending_len = data_len;
	piv->pending_offset = 0u;
	return emit_pending(piv, requested, response, response_cap, response_len);
}

/**
 * Return true if the data buffer matches the PIV AID or the PIV AID truncated form.
 */
static bool aid_matches(const uint8_t *data, size_t len)
{
	return (len == sizeof(s_piv_aid) &&
		memcmp(data, s_piv_aid, sizeof(s_piv_aid)) == 0) ||
	       (len == sizeof(s_piv_aid_truncated) &&
		memcmp(data, s_piv_aid_truncated,
		       sizeof(s_piv_aid_truncated)) == 0);
}

static int build_ccc(uint8_t *out, size_t cap, size_t *out_len)
{
	return wrap_tlv(0x53u, s_ccc_value, sizeof(s_ccc_value),
			out, cap, out_len);
}

static int build_chuid(struct piv_apdu *piv,
		       uint8_t *out, size_t cap, size_t *out_len)
{
	static const uint8_t expiration[] = {
		'2', '0', '4', '6', '0', '1', '0', '1',
	};
	uint8_t guid[16];
	uint8_t value[64];
	size_t at = 0u;

	if (piv->backend == NULL || piv->backend->get_guid == NULL ||
	    piv->backend->get_guid(piv->backend_ctx, guid) != 0) {
		return -1;
	}
	value[at++] = 0x30u;
	value[at++] = sizeof(s_zero_fascn);
	memcpy(value + at, s_zero_fascn, sizeof(s_zero_fascn));
	at += sizeof(s_zero_fascn);
	value[at++] = 0x34u;
	value[at++] = sizeof(guid);
	memcpy(value + at, guid, sizeof(guid));
	at += sizeof(guid);
	value[at++] = 0x35u;
	value[at++] = sizeof(expiration);
	memcpy(value + at, expiration, sizeof(expiration));
	at += sizeof(expiration);
	value[at++] = 0x3eu;
	value[at++] = 0x00u;
	value[at++] = 0xfeu;
	value[at++] = 0x00u;
	return wrap_tlv(0x53u, value, at, out, cap, out_len);
}

static int build_certificate(struct piv_apdu *piv, uint8_t key_ref,
			     uint8_t *out, size_t cap, size_t *out_len)
{
	const uint8_t *certificate;
	size_t certificate_len;
	size_t cert_length_len;
	size_t inner_len;
	size_t outer_length_len;
	size_t at = 0u;

	if (piv->backend == NULL || piv->backend->get_certificate == NULL ||
	    piv->backend->get_certificate(piv->backend_ctx, key_ref, &certificate,
					  &certificate_len) != 0 ||
	    certificate == NULL || certificate_len == 0u) {
		return -1;
	}
	cert_length_len = ber_length_bytes(certificate_len);
	inner_len = 1u + cert_length_len + certificate_len + 3u + 2u;
	outer_length_len = ber_length_bytes(inner_len);
	if (cap < 1u + outer_length_len + inner_len) {
		return -1;
	}
	out[at++] = 0x53u;
	if (put_ber_length(out + at, cap - at, inner_len,
			   &outer_length_len) != 0) {
		return -1;
	}
	at += outer_length_len;
	out[at++] = 0x70u;
	if (put_ber_length(out + at, cap - at, certificate_len,
			   &cert_length_len) != 0) {
		return -1;
	}
	at += cert_length_len;
	memcpy(out + at, certificate, certificate_len);
	at += certificate_len;
	out[at++] = 0x71u;
	out[at++] = 0x01u;
	out[at++] = 0x00u; /* uncompressed DER */
	out[at++] = 0xfeu;
	out[at++] = 0x00u;
	*out_len = at;
	return 0;
}

static int handle_get_data(struct piv_apdu *piv,
			   const struct command_apdu *command,
			   uint8_t *response, size_t response_cap,
			   size_t *response_len)
{
	uint8_t object[PIV_APDU_MAX_RESPONSE];
	size_t object_len = 0u;
	size_t requested = command->le_present ? command->le : 256u;

	if (command->p1 != 0x3fu || command->p2 != 0xffu ||
	    command->data_len < 3u || command->data[0] != 0x5cu ||
	    command->data[1] != command->data_len - 2u) {
		return finish(NULL, 0u, SW_BAD_PARAMETERS,
			      response, response_cap, response_len);
	}
	if (command->data_len == 3u && command->data[2] == 0x7eu) {
		return send_data(piv, s_discovery_object,
				 sizeof(s_discovery_object), requested,
				 response, response_cap, response_len);
	}
	if (command->data_len != 5u) {
		return finish(NULL, 0u, SW_FILE_NOT_FOUND,
			      response, response_cap, response_len);
	}
	if (memcmp(command->data + 2u,
		   (uint8_t[]){0x5f, 0xc1, 0x07}, 3u) == 0) {
		if (build_ccc(object, sizeof(object), &object_len) != 0) {
			return -1;
		}
	} else if (memcmp(command->data + 2u,
			  (uint8_t[]){0x5f, 0xc1, 0x02}, 3u) == 0) {
		if (build_chuid(piv, object, sizeof(object), &object_len) != 0) {
			return -1;
		}
	} else if (memcmp(command->data + 2u,
			  (uint8_t[]){0x5f, 0xc1, 0x05}, 3u) == 0) {
		if (build_certificate(piv, PIV_KEY_REF_AUTH,
				      object, sizeof(object),
				      &object_len) != 0) {
			return finish(NULL, 0u, SW_FILE_NOT_FOUND,
				      response, response_cap, response_len);
		}
	} else if (memcmp(command->data + 2u,
			  (uint8_t[]){0x5f, 0xc1, 0x0b}, 3u) == 0) {
		if (build_certificate(piv, PIV_KEY_REF_KEY_MANAGEMENT,
				      object, sizeof(object),
				      &object_len) != 0) {
			return finish(NULL, 0u, SW_FILE_NOT_FOUND,
				      response, response_cap, response_len);
		}
	} else {
		return finish(NULL, 0u, SW_FILE_NOT_FOUND,
			      response, response_cap, response_len);
	}
	return send_data(piv, object, object_len, requested,
			 response, response_cap, response_len);
}

/**
 * Return the status word for a PIN verify or change result: 0x9000 for success, 0x63Cn for retries
 * remaining, 0x6983 if blocked, or 0x6982 otherwise.
 */
static uint16_t pin_result_status(int result, uint8_t retries)
{
	if (result == 0) {
		return SW_SUCCESS;
	}
	if (result == 1) {
		return (uint16_t)(0x63c0u | (retries & 0x0fu));
	}
	if (result == -2) {
		return SW_AUTH_BLOCKED;
	}
	return SW_SECURITY_NOT_SATISFIED;
}

static int handle_verify(struct piv_apdu *piv,
			 const struct command_apdu *command,
			 uint8_t *response, size_t response_cap,
			 size_t *response_len)
{
	uint8_t retries = 0u;
	int result;

	if (command->p2 != PIV_KEY_REF_PIN) {
		return finish(NULL, 0u, SW_REFERENCE_NOT_FOUND,
			      response, response_cap, response_len);
	}
	if (command->p1 == 0xffu && command->data_len == 0u) {
		piv->pin_verified = false;
		return finish(NULL, 0u, SW_SUCCESS,
			      response, response_cap, response_len);
	}
	if (command->p1 != 0x00u) {
		return finish(NULL, 0u, SW_BAD_PARAMETERS,
			      response, response_cap, response_len);
	}
	if (command->data_len == 0u) {
		if (piv->pin_verified) {
			return finish(NULL, 0u, SW_SUCCESS,
				      response, response_cap, response_len);
		}
		if (piv->backend == NULL || piv->backend->pin_status == NULL) {
			return finish(NULL, 0u, SW_AUTH_BLOCKED,
				      response, response_cap, response_len);
		}
		result = piv->backend->pin_status(piv->backend_ctx, &retries);
		return finish(NULL, 0u,
			      result == 0 ?
			      (uint16_t)(0x63c0u | (retries & 0x0fu)) :
			      pin_result_status(result, retries),
			      response, response_cap, response_len);
	}
	if (command->data_len != PIV_PIN_BYTES ||
	    piv->backend == NULL || piv->backend->verify_pin == NULL) {
		return finish(NULL, 0u, SW_BAD_DATA,
			      response, response_cap, response_len);
	}
	result = piv->backend->verify_pin(piv->backend_ctx,
					 command->data, &retries);
	piv->pin_verified = result == 0;
	return finish(NULL, 0u, pin_result_status(result, retries),
		      response, response_cap, response_len);
}

static int handle_change_pin(struct piv_apdu *piv,
			     const struct command_apdu *command,
			     uint8_t *response, size_t response_cap,
			     size_t *response_len)
{
	uint8_t retries = 0u;
	int result;

	if (command->p1 != 0x00u || command->p2 != PIV_KEY_REF_PIN) {
		return finish(NULL, 0u, SW_BAD_PARAMETERS,
			      response, response_cap, response_len);
	}
	if (command->data_len != 2u * PIV_PIN_BYTES ||
	    piv->backend == NULL || piv->backend->change_pin == NULL) {
		return finish(NULL, 0u, SW_BAD_DATA,
			      response, response_cap, response_len);
	}
	result = piv->backend->change_pin(piv->backend_ctx,
					 command->data,
					 command->data + PIV_PIN_BYTES,
					 &retries);
	piv->pin_verified = result == 0;
	return finish(NULL, 0u, pin_result_status(result, retries),
		      response, response_cap, response_len);
}

/**
 * Encode a 32-byte value as a DER integer with a leading zero byte if the high bit is set,
 * returning the encoded length.
 */
static size_t der_integer(const uint8_t value[32], uint8_t out[35])
{
	size_t first = 0u;
	size_t len;
	bool leading_zero;

	while (first < 31u && value[first] == 0u) {
		first++;
	}
	len = 32u - first;
	leading_zero = (value[first] & 0x80u) != 0u;
	out[0] = 0x02u;
	out[1] = (uint8_t)(len + (leading_zero ? 1u : 0u));
	if (leading_zero) {
		out[2] = 0x00u;
		memcpy(out + 3u, value + first, len);
		return 3u + len;
	}
	memcpy(out + 2u, value + first, len);
	return 2u + len;
}

static int handle_general_authenticate(struct piv_apdu *piv,
				       const struct command_apdu *command,
				       uint8_t *response, size_t response_cap,
				       size_t *response_len)
{
	uint8_t raw[PIV_P256_RAW_SIGNATURE_BYTES];
	uint8_t r[35];
	uint8_t s[35];
	uint8_t der[72];
	uint8_t dynamic[76];
	size_t r_len;
	size_t s_len;
	size_t der_len;
	size_t dynamic_len;

	if (command->p1 != PIV_ALG_ECC_P256) {
		return finish(NULL, 0u, SW_BAD_PARAMETERS,
			      response, response_cap, response_len);
	}
	if (piv->pin_required && !piv->pin_verified) {
		return finish(NULL, 0u, SW_SECURITY_NOT_SATISFIED,
			      response, response_cap, response_len);
	}
	if (command->p2 == PIV_KEY_REF_KEY_MANAGEMENT) {
		uint8_t shared[PIV_P256_SHARED_SECRET_BYTES];
		uint8_t result[36];

		if (command->data_len != 71u ||
		    command->data[0] != 0x7cu || command->data[1] != 0x45u ||
		    command->data[2] != 0x82u || command->data[3] != 0x00u ||
		    command->data[4] != 0x85u || command->data[5] != 0x41u ||
		    command->data[6] != 0x04u ||
		    piv->backend == NULL ||
		    piv->backend->derive_shared == NULL ||
		    piv->backend->derive_shared(piv->backend_ctx,
						command->data + 6u,
						shared) != 0) {
			memset(shared, 0, sizeof(shared));
			return finish(NULL, 0u, SW_BAD_DATA,
				      response, response_cap, response_len);
		}
		result[0] = 0x7cu;
		result[1] = 0x22u;
		result[2] = 0x82u;
		result[3] = PIV_P256_SHARED_SECRET_BYTES;
		memcpy(result + 4u, shared, sizeof(shared));
		memset(shared, 0, sizeof(shared));
		return send_data(piv, result, sizeof(result),
				 command->le_present ? command->le : 256u,
				 response, response_cap, response_len);
	}
	if (command->p2 != PIV_KEY_REF_AUTH) {
		return finish(NULL, 0u, SW_BAD_PARAMETERS,
			      response, response_cap, response_len);
	}
	if (command->data_len != 38u ||
	    command->data[0] != 0x7cu || command->data[1] != 0x24u ||
	    command->data[2] != 0x82u || command->data[3] != 0x00u ||
	    command->data[4] != 0x81u ||
	    command->data[5] != PIV_P256_HASH_BYTES) {
		return finish(NULL, 0u, SW_BAD_DATA,
			      response, response_cap, response_len);
	}
	if (piv->backend == NULL || piv->backend->sign_hash == NULL ||
	    piv->backend->sign_hash(piv->backend_ctx, command->data + 6u,
				    raw) != 0) {
		return finish(NULL, 0u, SW_SECURITY_NOT_SATISFIED,
			      response, response_cap, response_len);
	}

	r_len = der_integer(raw, r);
	s_len = der_integer(raw + 32u, s);
	der[0] = 0x30u;
	der[1] = (uint8_t)(r_len + s_len);
	memcpy(der + 2u, r, r_len);
	memcpy(der + 2u + r_len, s, s_len);
	der_len = 2u + r_len + s_len;

	dynamic[0] = 0x7cu;
	dynamic[1] = (uint8_t)(2u + der_len);
	dynamic[2] = 0x82u;
	dynamic[3] = (uint8_t)der_len;
	memcpy(dynamic + 4u, der, der_len);
	dynamic_len = 4u + der_len;
	return send_data(piv, dynamic, dynamic_len,
			 command->le_present ? command->le : 256u,
			 response, response_cap, response_len);
}

/**
 * Initialize a PIV APDU engine with a backend, backend context, and a flag indicating whether PIN
 * is required for presence signatures.
 */
void piv_apdu_init(struct piv_apdu *piv,
		   const struct piv_apdu_backend *backend, void *backend_ctx,
		   bool pin_required)
{
	if (piv == NULL) {
		return;
	}
	memset(piv, 0, sizeof(*piv));
	piv->backend = backend;
	piv->backend_ctx = backend_ctx;
	piv->pin_required = pin_required;
}

/**
 * Reset the PIV APDU engine state: clear selection, PIN verification, and pending data.
 */
void piv_apdu_reset(struct piv_apdu *piv)
{
	if (piv == NULL) {
		return;
	}
	piv->selected = false;
	piv->pin_verified = false;
	piv->pending_offset = 0u;
	piv->pending_len = 0u;
}

int piv_apdu_transmit(struct piv_apdu *piv,
		      const uint8_t *command, size_t command_len,
		      uint8_t *response, size_t response_cap,
		      size_t *response_len)
{
	struct command_apdu apdu;

	if (piv == NULL || command == NULL || response == NULL ||
	    response_len == NULL) {
		return -1;
	}
	*response_len = 0u;
	if (parse_command(command, command_len, &apdu) != 0) {
		return finish(NULL, 0u, SW_WRONG_LENGTH,
			      response, response_cap, response_len);
	}
	if (apdu.cla != 0x00u) {
		return finish(NULL, 0u, SW_CLA_NOT_SUPPORTED,
			      response, response_cap, response_len);
	}

	if (apdu.ins == 0xc0u) {
		if (apdu.p1 != 0x00u || apdu.p2 != 0x00u ||
		    !apdu.le_present || piv->pending_len == 0u) {
			return finish(NULL, 0u, SW_BAD_PARAMETERS,
				      response, response_cap, response_len);
		}
		return emit_pending(piv, apdu.le, response,
				    response_cap, response_len);
	}
	piv->pending_offset = 0u;
	piv->pending_len = 0u;

	if (apdu.ins == 0xa4u) {
		if (apdu.p1 != 0x04u || apdu.p2 != 0x00u ||
		    !aid_matches(apdu.data, apdu.data_len)) {
			return finish(NULL, 0u, SW_FILE_NOT_FOUND,
				      response, response_cap, response_len);
		}
		piv->selected = true;
		return send_data(piv, s_piv_application_properties,
				 sizeof(s_piv_application_properties),
				 apdu.le_present ? apdu.le : 256u,
				 response, response_cap, response_len);
	}
	if (!piv->selected) {
		return finish(NULL, 0u, SW_APP_NOT_SELECTED,
			      response, response_cap, response_len);
	}

	switch (apdu.ins) {
	case 0xcbu:
		return handle_get_data(piv, &apdu, response,
				       response_cap, response_len);
	case 0x20u:
		return handle_verify(piv, &apdu, response,
				     response_cap, response_len);
	case 0x24u:
		return handle_change_pin(piv, &apdu, response,
					 response_cap, response_len);
	case 0x87u:
		return handle_general_authenticate(piv, &apdu, response,
						   response_cap, response_len);
	default:
		return finish(NULL, 0u, SW_INS_NOT_SUPPORTED,
			      response, response_cap, response_len);
	}
}
