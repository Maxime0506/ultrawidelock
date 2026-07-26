/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host test for the ESP32 presence console seam. The reader and UWB fakes make
 * the freshness checkpoints explicit; the real assertion codec and hash prove
 * that only a new trusted auth + range reaches the signing callback.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aliro_assert.h"
#include "aliro_assert_ec.h"
#include "aliro_prim.h"
#include "aliro_reader.h"
#include "nvs.h"
#include "presence_link.h"
#include "woz_uwb_facade.h"

static int fails;

static void okc(const char *name, bool condition)
{
	if (!condition) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* ---- crypto + persistent-key doubles ---------------------------------- */

static int sign_calls;
static uint8_t signed_prefix[ALIRO_ASSERT_SIGNED_LEN];

int aliro_prim_init(void)
{
	return 0;
}

int aliro_ec_p256_keygen(uint8_t priv[ALIRO_P256_SCALAR], uint8_t pub[ALIRO_P256_POINT])
{
	memset(priv, 0x33, ALIRO_P256_SCALAR);
	pub[0] = 0x04;
	memset(pub + 1, 0x44, ALIRO_P256_POINT - 1);
	return 0;
}

int aliro_ec_p256_pub_from_priv(const uint8_t priv[ALIRO_P256_SCALAR],
				uint8_t pub[ALIRO_P256_POINT])
{
	(void)priv;
	pub[0] = 0x04;
	memset(pub + 1, 0x44, ALIRO_P256_POINT - 1);
	return 0;
}

int aliro_assert_ec_sign(void *ctx, const uint8_t *msg, size_t msg_len,
			 uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	(void)ctx;
	if (msg_len != sizeof(signed_prefix)) {
		return -1;
	}
	memcpy(signed_prefix, msg, msg_len);
	memset(sig, 0x5a, ALIRO_ASSERT_SIG_LEN);
	sign_calls++;
	return 0;
}

/* ---- reader + range doubles ------------------------------------------- */

static bool have_expected;
static bool reset_ready;
static bool auth_fresh;
static bool range_fresh;
static int32_t range_cm;
static uint32_t range_generation;
static int restart_calls;
static int wallet_grants;
static uint8_t expected_pub[ALIRO_ASSERT_PUB_LEN];
static uint8_t actual_pub[ALIRO_ASSERT_PUB_LEN];

void aliro_reader_notify_unlock(bool unsecured)
{
	wallet_grants += unsecured ? 1 : -1;
}

bool aliro_reader_presence_expected_credential(uint8_t out[ALIRO_ASSERT_PUB_LEN])
{
	if (have_expected) {
		memcpy(out, expected_pub, sizeof(expected_pub));
	}
	return have_expected;
}

uint32_t aliro_reader_presence_restart(void)
{
	restart_calls++;
	return (uint32_t)restart_calls;
}

bool aliro_reader_presence_checkpoint(uint32_t request, uint32_t *auth_generation)
{
	if (!reset_ready || request != (uint32_t)restart_calls) {
		return false;
	}
	*auth_generation = 41u;
	return true;
}

bool aliro_reader_presence_authenticated_after(uint32_t checkpoint,
					       uint8_t out[ALIRO_ASSERT_PUB_LEN])
{
	if (!auth_fresh || checkpoint != 41u) {
		return false;
	}
	memcpy(out, actual_pub, sizeof(actual_pub));
	return true;
}

uint32_t woz_uwb_range_generation(void)
{
	return range_generation;
}

bool woz_uwb_trusted_range_after_cm(int32_t *cm_out, uint32_t after)
{
	if (!range_fresh || after != range_generation) {
		return false;
	}
	*cm_out = range_cm;
	return true;
}

static void reset_scenario(void)
{
	have_expected = true;
	reset_ready = true;
	auth_fresh = true;
	range_fresh = true;
	range_cm = 25;
	range_generation++;
	expected_pub[0] = 0x04;
	memset(expected_pub + 1, 0x61, sizeof(expected_pub) - 1);
	memcpy(actual_pub, expected_pub, sizeof(actual_pub));
	sign_calls = 0;
	memset(signed_prefix, 0, sizeof(signed_prefix));
}

static int prove(void)
{
	char nonce[] = "000102030405060708090a0b0c0d0e0f";
	char *argv[] = {"presence", "prove", nonce};

	return presence_link_cmd(3, argv);
}

int main(void)
{
	uint8_t expected_id[ALIRO_ASSERT_CREDID_LEN];

	fake_nvs_reset();
	presence_link_init(true);

	printf("-- fresh success --\n");
	reset_scenario();
	okc("success.rc", prove() == 0);
	okc("success.signed_once", sign_calls == 1);
	okc("success.present", signed_prefix[4] == ALIRO_PRESENCE_PRESENT);
	okc("success.nonce", memcmp(signed_prefix + 5,
				    "\x00\x01\x02\x03\x04\x05\x06\x07"
				    "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
				    ALIRO_ASSERT_NONCE_LEN) == 0);
	aliro_assert_cred_id(expected_pub, expected_id);
	okc("success.credential", memcmp(signed_prefix + 21, expected_id,
					 ALIRO_ASSERT_CREDID_LEN) == 0);
	okc("success.distance", signed_prefix[29] == 0 && signed_prefix[30] == 25);
	okc("success.wallet_after_sign", wallet_grants == 1);

	printf("-- stale range --\n");
	reset_scenario();
	range_fresh = false;
	okc("stale_range.rc", prove() != 0);
	okc("stale_range.no_signature", sign_calls == 0);

	printf("-- stale auth --\n");
	reset_scenario();
	auth_fresh = false;
	okc("stale_auth.rc", prove() != 0);
	okc("stale_auth.no_signature", sign_calls == 0);

	printf("-- wrong credential --\n");
	reset_scenario();
	actual_pub[1] ^= 0xff;
	okc("wrong_credential.rc", prove() != 0);
	okc("wrong_credential.no_signature", sign_calls == 0);

	printf("-- too far --\n");
	reset_scenario();
	range_cm = 41;
	okc("too_far.rc", prove() != 0);
	okc("too_far.no_signature", sign_calls == 0);

	printf("-- reset timeout --\n");
	reset_scenario();
	reset_ready = false;
	okc("reset_timeout.rc", prove() != 0);
	okc("reset_timeout.no_signature", sign_calls == 0);

	printf("-- ambiguous trust --\n");
	reset_scenario();
	have_expected = false;
	okc("ambiguous.rc", prove() != 0);
	okc("ambiguous.no_restart", restart_calls == 6);
	okc("ambiguous.no_signature", sign_calls == 0);

	printf("RESULT: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
