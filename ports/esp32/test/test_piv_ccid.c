#include "piv_apdu.h"
#include "piv_ccid.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

/* memmem is a GNU extension, not C. macOS declares it from <string.h>
 * unconditionally while glibc hides it unless _GNU_SOURCE is defined before the
 * first system header, so calling it here built clean on a developer's Mac and
 * failed the Linux CI runner with an implicit declaration. Two call sites do not
 * justify a feature-test macro that every future include in this file would then
 * have to stay behind. */
static const uint8_t *find_bytes(const uint8_t *hay, size_t hay_len, const uint8_t *needle,
				 size_t needle_len)
{
	if (needle_len > hay_len) {
		return NULL;
	}
	for (size_t i = 0; i + needle_len <= hay_len; i++) {
		if (memcmp(hay + i, needle, needle_len) == 0) {
			return hay + i;
		}
	}
	return NULL;
}

#define CHECK(cond, label) do {                                                \
	if (cond) {                                                            \
		printf("  ok  %s\n", label);                                    \
	} else {                                                               \
		printf("  FAIL %s\n", label);                                   \
		failures++;                                                        \
	}                                                                      \
} while (0)

struct fake_identity {
	uint8_t auth_certificate[300];
	uint8_t key_management_certificate[220];
	uint8_t guid[16];
	uint8_t pin[PIV_PIN_BYTES];
	bool provisioned;
	uint8_t retries;
	bool allow_signature;
	unsigned int sign_calls;
	uint8_t last_hash[PIV_P256_HASH_BYTES];
	unsigned int derive_calls;
	uint8_t last_peer_public_key[PIV_P256_POINT_BYTES];
};

static int fake_get_certificate(void *ctx, uint8_t key_ref,
				const uint8_t **certificate,
				size_t *certificate_len)
{
	struct fake_identity *identity = ctx;

	if (key_ref == PIV_KEY_REF_AUTH) {
		*certificate = identity->auth_certificate;
		*certificate_len = sizeof(identity->auth_certificate);
	} else if (key_ref == PIV_KEY_REF_KEY_MANAGEMENT) {
		*certificate = identity->key_management_certificate;
		*certificate_len =
			sizeof(identity->key_management_certificate);
	} else {
		return -1;
	}
	return 0;
}

static int fake_get_guid(void *ctx, uint8_t guid[16])
{
	struct fake_identity *identity = ctx;

	memcpy(guid, identity->guid, sizeof(identity->guid));
	return 0;
}

static int fake_pin_status(void *ctx, uint8_t *retries)
{
	struct fake_identity *identity = ctx;

	*retries = identity->retries;
	return identity->provisioned && identity->retries != 0u ? 0 : -2;
}

static int fake_verify_pin(void *ctx,
			   const uint8_t pin[PIV_PIN_BYTES],
			   uint8_t *retries)
{
	struct fake_identity *identity = ctx;

	if (!identity->provisioned || identity->retries == 0u) {
		*retries = identity->retries;
		return -2;
	}
	if (memcmp(pin, identity->pin, PIV_PIN_BYTES) != 0) {
		identity->retries--;
		*retries = identity->retries;
		return identity->retries == 0u ? -2 : 1;
	}
	identity->retries = 3u;
	*retries = identity->retries;
	return 0;
}

static int fake_change_pin(void *ctx,
			   const uint8_t old_pin[PIV_PIN_BYTES],
			   const uint8_t new_pin[PIV_PIN_BYTES],
			   uint8_t *retries)
{
	static const uint8_t unset[PIV_PIN_BYTES] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};
	struct fake_identity *identity = ctx;
	bool old_matches = identity->provisioned ?
		memcmp(old_pin, identity->pin, PIV_PIN_BYTES) == 0 :
		memcmp(old_pin, unset, sizeof(unset)) == 0;

	if (!old_matches) {
		if (identity->provisioned && identity->retries != 0u) {
			identity->retries--;
		}
		*retries = identity->retries;
		return identity->retries == 0u ? -2 : 1;
	}
	memcpy(identity->pin, new_pin, PIV_PIN_BYTES);
	identity->provisioned = true;
	identity->retries = 3u;
	*retries = identity->retries;
	return 0;
}

static int fake_sign_hash(void *ctx,
			  const uint8_t hash[PIV_P256_HASH_BYTES],
			  uint8_t signature[PIV_P256_RAW_SIGNATURE_BYTES])
{
	struct fake_identity *identity = ctx;

	identity->sign_calls++;
	memcpy(identity->last_hash, hash, sizeof(identity->last_hash));
	if (!identity->allow_signature) {
		return -1;
	}
	memset(signature, 0x11, PIV_P256_RAW_SIGNATURE_BYTES);
	signature[0] = 0x80u;
	signature[32] = 0x00u;
	signature[33] = 0x7fu;
	return 0;
}

static int fake_derive_shared(
	void *ctx, const uint8_t peer_public_key[PIV_P256_POINT_BYTES],
	uint8_t shared_secret[PIV_P256_SHARED_SECRET_BYTES])
{
	struct fake_identity *identity = ctx;

	identity->derive_calls++;
	memcpy(identity->last_peer_public_key, peer_public_key,
	       sizeof(identity->last_peer_public_key));
	memset(shared_secret, 0x5a, PIV_P256_SHARED_SECRET_BYTES);
	return 0;
}

static const struct piv_apdu_backend s_backend = {
	.get_certificate = fake_get_certificate,
	.get_guid = fake_get_guid,
	.pin_status = fake_pin_status,
	.verify_pin = fake_verify_pin,
	.change_pin = fake_change_pin,
	.sign_hash = fake_sign_hash,
	.derive_shared = fake_derive_shared,
};

static void fake_identity_init(struct fake_identity *identity)
{
	memset(identity, 0, sizeof(*identity));
	for (size_t i = 0u; i < sizeof(identity->auth_certificate); i++) {
		identity->auth_certificate[i] = (uint8_t)i;
	}
	for (size_t i = 0u;
	     i < sizeof(identity->key_management_certificate); i++) {
		identity->key_management_certificate[i] =
			(uint8_t)(0xe0u + i);
	}
	for (size_t i = 0u; i < sizeof(identity->guid); i++) {
		identity->guid[i] = (uint8_t)(0xa0u + i);
	}
	identity->retries = 3u;
}

static size_t request(uint8_t *out, uint8_t type, uint32_t payload_len,
		      uint8_t slot, uint8_t seq, uint8_t p0,
		      const uint8_t *payload)
{
	out[0] = type;
	out[1] = (uint8_t)payload_len;
	out[2] = (uint8_t)(payload_len >> 8);
	out[3] = (uint8_t)(payload_len >> 16);
	out[4] = (uint8_t)(payload_len >> 24);
	out[5] = slot;
	out[6] = seq;
	out[7] = p0;
	out[8] = 0;
	out[9] = 0;
	if (payload_len != 0u && payload != NULL) {
		memcpy(out + PIV_CCID_HEADER_LEN, payload, payload_len);
	}
	return PIV_CCID_HEADER_LEN + payload_len;
}

static uint32_t response_payload_len(const uint8_t *response)
{
	return (uint32_t)response[1] |
	       ((uint32_t)response[2] << 8) |
	       ((uint32_t)response[3] << 16) |
	       ((uint32_t)response[4] << 24);
}

static void test_slot_and_power(void)
{
	struct piv_ccid ccid;
	struct fake_identity identity;
	uint8_t req[128];
	uint8_t rsp[128];
	size_t req_len;
	size_t rsp_len = 0;

	fake_identity_init(&identity);
	piv_ccid_init(&ccid, &s_backend, &identity, true);
	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  0, 0, 7, 0, NULL);
	CHECK(piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp),
			       &rsp_len) == 0,
	      "slot-status request is framed");
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_SLOT_STATUS &&
	      rsp[6] == 7 && rsp[7] == 1 && rsp_len == PIV_CCID_HEADER_LEN,
	      "slot starts present and inactive, sequence echoed");

	req_len = request(req, PIV_CCID_PC_TO_RDR_ICC_POWER_ON,
			  0, 0, 8, 0, NULL);
	CHECK(piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp),
			       &rsp_len) == 0,
	      "power-on request succeeds");
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_DATA_BLOCK &&
	      rsp[7] == 0 && response_payload_len(rsp) == 4 &&
	      memcmp(rsp + PIV_CCID_HEADER_LEN,
		     (uint8_t[]){0x3b, 0x80, 0x01, 0x81}, 4) == 0,
	      "power-on returns a valid minimal T=1 ATR");

	ccid.piv.pin_verified = true;
	req_len = request(req, PIV_CCID_PC_TO_RDR_ICC_POWER_OFF,
			  0, 0, 9, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_SLOT_STATUS && rsp[7] == 1,
	      "power-off returns the card to inactive");
	CHECK(!ccid.piv.pin_verified,
	      "power-off clears the PIN verification state");
}

static void select_piv(struct piv_apdu *piv)
{
	static const uint8_t select[] = {
		0x00, 0xa4, 0x04, 0x00, 0x09,
		0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00,
	};
	uint8_t response[128];
	size_t response_len = 0u;

	CHECK(piv_apdu_transmit(piv, select, sizeof(select),
				response, sizeof(response), &response_len) == 0 &&
	      response_len > 2u &&
	      response[0] == 0x61u &&
	      response[response_len - 2u] == 0x90u &&
	      response[response_len - 1u] == 0x00u &&
	      piv->selected,
	      "right-truncated PIV AID returns an application template");
}

static void test_objects_and_chaining(void)
{
	static const uint8_t get_discovery[] = {
		0x00, 0xcb, 0x3f, 0xff, 0x03, 0x5c, 0x01, 0x7e,
	};
	static const uint8_t get_chuid[] = {
		0x00, 0xcb, 0x3f, 0xff, 0x05, 0x5c, 0x03,
		0x5f, 0xc1, 0x02,
	};
	static const uint8_t get_cert[] = {
		0x00, 0xcb, 0x3f, 0xff, 0x05, 0x5c, 0x03,
		0x5f, 0xc1, 0x05,
	};
	static const uint8_t get_key_management_cert[] = {
		0x00, 0xcb, 0x3f, 0xff, 0x05, 0x5c, 0x03,
		0x5f, 0xc1, 0x0b,
	};
	static const uint8_t get_digital_signature_cert[] = {
		0x00, 0xcb, 0x3f, 0xff, 0x05, 0x5c, 0x03,
		0x5f, 0xc1, 0x0a,
	};
	static const uint8_t get_response[] = {
		0x00, 0xc0, 0x00, 0x00, 0x00,
	};
	struct fake_identity identity;
	struct piv_apdu piv;
	uint8_t response[PIV_APDU_MAX_RESPONSE + 2u];
	size_t response_len = 0u;
	size_t first_payload;

	fake_identity_init(&identity);
	piv_apdu_init(&piv, &s_backend, &identity, true);
	select_piv(&piv);

	CHECK(piv_apdu_transmit(&piv, get_discovery, sizeof(get_discovery),
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x7eu &&
	      response[response_len - 2u] == 0x90u,
	      "GET DATA exposes the PIV discovery object");
	CHECK(piv_apdu_transmit(&piv, get_chuid, sizeof(get_chuid),
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x53u &&
	      find_bytes(response, response_len - 2u,
			 identity.guid, sizeof(identity.guid)) != NULL,
	      "CHUID contains the persistent anonymous GUID");

	CHECK(piv_apdu_transmit(&piv, get_cert, sizeof(get_cert),
				response, sizeof(response), &response_len) == 0 &&
	      response_len == 258u &&
	      response[response_len - 2u] == 0x61u,
	      "large slot 9A certificate starts a chained response");
	first_payload = response_len - 2u;
	CHECK(piv_apdu_transmit(&piv, get_response, sizeof(get_response),
				response, sizeof(response), &response_len) == 0 &&
	      response_len > 2u &&
	      response[response_len - 2u] == 0x90u &&
	      first_payload + response_len - 2u >
		      sizeof(identity.auth_certificate),
	      "GET RESPONSE completes the certificate object");

	CHECK(piv_apdu_transmit(&piv, get_key_management_cert,
				sizeof(get_key_management_cert),
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x53u &&
	      find_bytes(response, response_len - 2u,
			 identity.key_management_certificate,
			 sizeof(identity.key_management_certificate)) != NULL &&
	      response[response_len - 2u] == 0x90u,
	      "slot 9D exposes a distinct key-management certificate");
	CHECK(piv_apdu_transmit(&piv, get_digital_signature_cert,
				sizeof(get_digital_signature_cert),
				response, sizeof(response), &response_len) == 0 &&
	      response_len == 2u &&
	      response[0] == 0x6au && response[1] == 0x82u,
	      "absent slot 9C does not masquerade as key management");
}

static void test_pin_and_presence_signature_boundary(void)
{
	static const uint8_t verify_status[] = {
		0x00, 0x20, 0x00, 0x80,
	};
	static const uint8_t new_pin[PIV_PIN_BYTES] = {
		'1', '2', '3', '4', '5', '6', 0xff, 0xff,
	};
	static const uint8_t wrong_pin[PIV_PIN_BYTES] = {
		'0', '0', '0', '0', '0', '0', 0xff, 0xff,
	};
	struct fake_identity identity;
	struct piv_apdu piv;
	uint8_t command[80];
	uint8_t response[128];
	size_t response_len = 0u;

	fake_identity_init(&identity);
	piv_apdu_init(&piv, &s_backend, &identity, true);
	select_piv(&piv);
	CHECK(piv_apdu_transmit(&piv, verify_status, sizeof(verify_status),
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x69u && response[1] == 0x83u,
	      "an unprovisioned PIN is unusable");

	command[0] = 0x00u;
	command[1] = 0x24u;
	command[2] = 0x00u;
	command[3] = 0x80u;
	command[4] = 16u;
	memset(command + 5u, 0xff, PIV_PIN_BYTES);
	memcpy(command + 5u + PIV_PIN_BYTES, new_pin, PIV_PIN_BYTES);
	CHECK(piv_apdu_transmit(&piv, command, 21u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x90u && response[1] == 0x00u &&
	      identity.provisioned,
	      "CHANGE REFERENCE DATA bootstraps a user-selected PIN");

	command[1] = 0x20u;
	command[4] = PIV_PIN_BYTES;
	memcpy(command + 5u, wrong_pin, PIV_PIN_BYTES);
	CHECK(piv_apdu_transmit(&piv, command, 13u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x63u && response[1] == 0xc2u &&
	      !piv.pin_verified,
	      "wrong PIN decrements retries and does not authenticate");

	memcpy(command + 5u, new_pin, PIV_PIN_BYTES);
	CHECK(piv_apdu_transmit(&piv, command, 13u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x90u && response[1] == 0x00u &&
	      piv.pin_verified,
	      "correct PIN establishes the card-session authorization");

	command[1] = 0x87u;
	command[2] = 0x11u;
	command[3] = 0x9au;
	command[4] = 38u;
	command[5] = 0x7cu;
	command[6] = 0x24u;
	command[7] = 0x82u;
	command[8] = 0x00u;
	command[9] = 0x81u;
	command[10] = PIV_P256_HASH_BYTES;
	for (size_t i = 0u; i < PIV_P256_HASH_BYTES; i++) {
		command[11u + i] = (uint8_t)(0x40u + i);
	}
	identity.allow_signature = false;
	CHECK(piv_apdu_transmit(&piv, command, 43u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x69u && response[1] == 0x82u &&
	      identity.sign_calls == 1u,
	      "failed fresh presence denies the private-key operation");

	identity.allow_signature = true;
	CHECK(piv_apdu_transmit(&piv, command, 43u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x7cu && response[2] == 0x82u &&
	      response[4] == 0x30u &&
	      response[response_len - 2u] == 0x90u &&
	      response[response_len - 1u] == 0x00u &&
	      identity.sign_calls == 2u &&
	      memcmp(identity.last_hash, command + 11u,
		     PIV_P256_HASH_BYTES) == 0,
	      "fresh presence releases one DER-encoded ECDSA signature");

	command[3] = PIV_KEY_REF_KEY_MANAGEMENT;
	command[4] = 71u;
	command[5] = 0x7cu;
	command[6] = 0x45u;
	command[7] = 0x82u;
	command[8] = 0x00u;
	command[9] = 0x85u;
	command[10] = 0x41u;
	command[11] = 0x04u;
	for (size_t i = 1u; i < PIV_P256_POINT_BYTES; i++) {
		command[11u + i] = (uint8_t)(0x20u + i);
	}
	CHECK(piv_apdu_transmit(&piv, command, 76u,
				response, sizeof(response), &response_len) == 0 &&
	      response_len == 38u &&
	      response[0] == 0x7cu && response[1] == 0x22u &&
	      response[2] == 0x82u && response[3] == 0x20u &&
	      response[4] == 0x5au &&
	      response[response_len - 2u] == 0x90u &&
	      identity.derive_calls == 1u &&
	      memcmp(identity.last_peer_public_key, command + 11u,
		     PIV_P256_POINT_BYTES) == 0,
	      "slot 9D performs the P-256 ECDH primitive");

	command[3] = PIV_KEY_REF_AUTH;
	command[4] = 38u;
	piv_apdu_reset(&piv);
	select_piv(&piv);
	CHECK(piv_apdu_transmit(&piv, command, 43u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x69u && response[1] == 0x82u &&
	      identity.sign_calls == 2u,
	      "a new card session requires PIN verification again");
}

static void test_pinless_uwb_policy(void)
{
	struct fake_identity identity;
	struct piv_apdu piv;
	uint8_t command[80] = {
		0x00u, 0x87u, 0x11u, PIV_KEY_REF_AUTH, 38u,
		0x7cu, 0x24u, 0x82u, 0x00u, 0x81u,
		PIV_P256_HASH_BYTES,
	};
	uint8_t response[128];
	size_t response_len = 0u;

	fake_identity_init(&identity);
	piv_apdu_init(&piv, &s_backend, &identity, false);
	select_piv(&piv);
	for (size_t i = 0u; i < PIV_P256_HASH_BYTES; i++) {
		command[11u + i] = (uint8_t)(0x60u + i);
	}

	identity.allow_signature = false;
	CHECK(piv_apdu_transmit(&piv, command, 43u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x69u && response[1] == 0x82u &&
	      identity.sign_calls == 1u,
	      "PIN-less policy still denies a failed fresh UWB proof");

	identity.allow_signature = true;
	CHECK(piv_apdu_transmit(&piv, command, 43u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x7cu &&
	      response[response_len - 2u] == 0x90u &&
	      identity.sign_calls == 2u,
	      "PIN-less policy releases 9A only after fresh UWB");

	piv_apdu_reset(&piv);
	select_piv(&piv);
	command[3] = PIV_KEY_REF_KEY_MANAGEMENT;
	command[4] = 71u;
	command[5] = 0x7cu;
	command[6] = 0x45u;
	command[7] = 0x82u;
	command[8] = 0x00u;
	command[9] = 0x85u;
	command[10] = 0x41u;
	command[11] = 0x04u;
	for (size_t i = 1u; i < PIV_P256_POINT_BYTES; i++) {
		command[11u + i] = (uint8_t)(0x30u + i);
	}
	CHECK(piv_apdu_transmit(&piv, command, 76u,
				response, sizeof(response), &response_len) == 0 &&
	      response[0] == 0x7cu && response[2] == 0x82u &&
	      response[response_len - 2u] == 0x90u &&
	      identity.derive_calls == 1u,
	      "PIN-less policy permits 9D keychain agreement");
}

static void test_rejections_and_parameters(void)
{
	struct piv_ccid ccid;
	struct fake_identity identity;
	uint8_t req[128] = {0};
	uint8_t rsp[128];
	size_t req_len;
	size_t rsp_len = 0;

	fake_identity_init(&identity);
	piv_ccid_init(&ccid, &s_backend, &identity, true);
	CHECK(piv_ccid_process(&ccid, req, 9, rsp, sizeof(rsp),
			       &rsp_len) == -1,
	      "truncated CCID header produces no guessed response");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  0, 1, 4, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0x05,
	      "nonzero slot is rejected");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  1, 0, 5, 0, NULL);
	piv_ccid_process(&ccid, req, PIV_CCID_HEADER_LEN,
			 rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0x01,
	      "declared payload length mismatch is rejected");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_PARAMETERS,
			  0, 0, 6, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_PARAMETERS &&
	      rsp[9] == 1 && response_payload_len(rsp) == 7,
	      "reader reports bounded T=1 parameters");

	req_len = request(req, 0xff, 0, 0, 7, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0,
	      "unsupported CCID command fails closed");
}

int main(void)
{
	printf("-- PIV CCID protocol core --\n");
	CHECK(PIV_CCID_FUNCTIONAL_DESCRIPTOR_TYPE == 0x21u,
	      "CCID functional descriptor uses USB-IF type 0x21");
	test_slot_and_power();
	test_objects_and_chaining();
	test_pin_and_presence_signature_boundary();
	test_pinless_uwb_policy();
	test_rejections_and_parameters();
	printf("RESULT %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
