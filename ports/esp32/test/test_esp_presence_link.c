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
static bool range_sts_ok;
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

bool woz_uwb_trusted_range_after_checked_cm(int32_t *cm_out, uint32_t after,
					    struct woz_uwb_range_integrity *ig_out)
{
	ig_out->sts_ok = false;
	ig_out->sts_quality = 0;
	ig_out->trust_level = 0u;
	if (!range_fresh || after != range_generation) {
		return false;
	}
	*cm_out = range_cm;
	ig_out->sts_ok = range_sts_ok;
	ig_out->sts_quality = range_sts_ok ? 120 : -7;
	ig_out->trust_level = 3u;
	return true;
}

static void reset_scenario(void)
{
	have_expected = true;
	reset_ready = true;
	auth_fresh = true;
	range_fresh = true;
	range_sts_ok = true;
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
	/* The evidence the range arrived with reaches the signed bytes, rather than
	 * the firmware asserting a fixed "all good" of its own. */
	okc("success.range_flags", signed_prefix[31] == ALIRO_ASSERT_RANGE_STS_OK);
	okc("success.sts_quality", signed_prefix[32] == 0 && signed_prefix[33] == 120);
	okc("success.trust_level", signed_prefix[34] == 3);
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

	printf("-- suspect STS --\n");
	reset_scenario();
	range_sts_ok = false;
	/* An in-threshold distance from a block whose STS did not correlate. The
	 * firmware must refuse to sign it at all rather than sign it with the bit
	 * clear and leave the refusal to whoever reads the frame: a proof that has
	 * left the device is one the device no longer controls. */
	okc("suspect_sts.rc", prove() != 0);
	okc("suspect_sts.no_signature", sign_calls == 0);
	okc("suspect_sts.no_wallet_grant", wallet_grants == 0);

	printf("-- reset timeout --\n");
	reset_scenario();
	reset_ready = false;
	okc("reset_timeout.rc", prove() != 0);
	okc("reset_timeout.no_signature", sign_calls == 0);

	printf("-- ambiguous trust --\n");
	reset_scenario();
	have_expected = false;
	int restarts_before = restart_calls;

	okc("ambiguous.rc", prove() != 0);
	/* Compared against the live count rather than a literal: the claim is that
	 * this scenario starts no transaction, which should not need re-deriving
	 * every time a scenario is added above it. */
	okc("ambiguous.no_restart", restart_calls == restarts_before);
	okc("ambiguous.no_signature", sign_calls == 0);

	printf("RESULT: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
