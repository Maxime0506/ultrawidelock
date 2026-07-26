// Presence dongle commands (see presence_link.h). `prove` ends every old Aliro
// link, waits for a new trusted credential authentication and a later trusted
// UWB range, then signs that post-challenge result under a persistent P-256 key.
//
// These live on the ordinary console rather than a private binary channel, so one
// board can be provisioned (aliro-import) and queried for presence without
// reflashing between modes, and so a stray log line is just another line instead of
// a corrupted frame.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stdio.h>
#include <string.h>

#include "nvs.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"
#include "aliro_assert.h"
#include "aliro_assert_ec.h"
#include "aliro_prim.h"
#include "presence_link.h"
#include "woz_port.h"

#define PRESENCE_NS     "presence"
#define PRESENCE_DEVKEY "kdev"

#if !defined(CONFIG_WOZ_PRESENCE_TIMEOUT_MS)
#define CONFIG_WOZ_PRESENCE_TIMEOUT_MS 8000
#endif
#if !defined(CONFIG_WOZ_PRESENCE_MAX_CM)
#define CONFIG_WOZ_PRESENCE_MAX_CM 40
#endif
#define PROOF_POLL_MS 20

static bool s_drive_wallet;

/* Device signing identity. Only the private scalar is persisted; the public point
 * is re-derived at every boot so the two can never drift apart in NVS. */
static struct aliro_assert_ec_priv s_dev;
static uint8_t s_dev_pub[ALIRO_ASSERT_PUB_LEN];
static bool s_dev_set;

// Load the device signing key from NVS, generating and persisting one on first
// boot. This key IS the dongle's identity to every third-party verifier, so it has
// to outlive reboots: a key regenerated each boot would silently invalidate every
// public key anyone had already enrolled.
static void load_or_make_dev_key(void)
{
	nvs_handle_t h;
	uint8_t priv[ALIRO_P256_SCALAR];
	size_t len = sizeof(priv);
	bool have = false;

	if (nvs_open(PRESENCE_NS, NVS_READONLY, &h) == ESP_OK) {
		have = nvs_get_blob(h, PRESENCE_DEVKEY, priv, &len) == ESP_OK &&
		       len == sizeof(priv);
		nvs_close(h);
	}

	if (have) {
		if (aliro_ec_p256_pub_from_priv(priv, s_dev_pub) != 0) {
			return;
		}
	} else {
		if (aliro_ec_p256_keygen(priv, s_dev_pub) != 0) {
			return;
		}
		if (nvs_open(PRESENCE_NS, NVS_READWRITE, &h) != ESP_OK) {
			return;
		}
		esp_err_t e = nvs_set_blob(h, PRESENCE_DEVKEY, priv, sizeof(priv));

		if (e == ESP_OK) {
			e = nvs_commit(h);
		}
		nvs_close(h);
		/* Refuse to sign under a key that did not reach flash. Handing out a
		 * public key the dongle forgets at the next reboot is worse than
		 * having no key at all: enrolment would appear to succeed. */
		if (e != ESP_OK) {
			memset(s_dev_pub, 0, sizeof(s_dev_pub));
			return;
		}
	}

	memcpy(s_dev.d, priv, sizeof(priv));
	s_dev_set = true;
}

void presence_link_init(bool drive_wallet_grant)
{
	/* Idempotent (psa_crypto_init is), and aliro_reader_start() has already run
	 * it. Repeated here so the keygen below does not depend on that ordering. */
	(void)aliro_prim_init();
	load_or_make_dev_key();
	s_drive_wallet = drive_wallet_grant;
	/* No range listener on purpose. The range latch already carries its own age,
	 * so presence can read freshness when a challenge arrives, and the single
	 * listener slot stays free for whichever app actually needs to wake on a
	 * range -- the Matter lock's approach loop does. */
}

/* Wallet unlock animation (Reader-Status-Changed, Aliro transaction step 23).
 * Latched so the grant fires on the edge, not once per challenge: the phone
 * animates when a presence check first succeeds and relocks when a later check
 * finds the range stale. Deliberately demand-driven -- no host challenge, no
 * animation -- so the phone confirms exactly the moment a proof was taken. */
static bool s_wallet_granted;

// Send the phone the grant/relock notification when the presence verdict changes.
// Runs in the console task, never on the UWB RX path, because the send seals on the
// BLE channel. A no-op with no established session (the reader logs and drops).
//
// Off unless the host app asked for it. An app with its own lock state already owns
// this notification (the Matter lock grants on its approach loop), and two owners
// would fight over what the phone is being told.
static void notify_wallet(bool present)
{
	if (!s_drive_wallet || present == s_wallet_granted) {
		return;
	}
	s_wallet_granted = present;
	aliro_reader_notify_unlock(present);
}

// Fill one successful assertion. Acquisition already proved that both the
// credential and range are post-challenge, so this function accepts no latch
// state and has no ABSENT path it could accidentally sign.
static void fill_assert(struct aliro_assert *a, const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN],
			const uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN], int32_t cm)
{
	memset(a, 0, sizeof(*a));
	memcpy(a->nonce, nonce, ALIRO_ASSERT_NONCE_LEN);
	memcpy(a->cred_id, cred_id, ALIRO_ASSERT_CREDID_LEN);
	a->status = ALIRO_PRESENCE_PRESENT;
	a->distance_cm = (cm > 0xFFFE) ? 0xFFFEu : (uint16_t)cm;
	a->uptime_ms = (uint64_t)woz_uptime_ms();
	/* unix_ms stays ALIRO_ASSERT_TIME_NONE (the memset above): this dongle has
	 * no trusted wall clock, and claiming one it cannot back would be worse
	 * than admitting it has none. Freshness therefore rests entirely on the
	 * verifier's nonce being unpredictable -- which holds for a third-party
	 * verifier of a P-256 frame exactly as it does for the paired host. */
}

// Emit one tagged hex line in a single printf. Assembling the line first matters:
// another task's output can land between two printf calls but not inside one, and
// the host frames on whole lines.
static void emit_hex(const char *tag, const uint8_t *b, size_t n)
{
	char line[2 * ALIRO_ASSERT_WIRE_P256 + 32];
	size_t at = (size_t)snprintf(line, sizeof(line), "%s ", tag);

	for (size_t i = 0; i < n && at + 3 <= sizeof(line); i++) {
		at += (size_t)snprintf(line + at, sizeof(line) - at, "%02x", b[i]);
	}
	printf("%s\n", line);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

// Parse exactly n bytes of hex. Rejects a short or long string rather than taking a
// prefix: a truncated nonce that still parsed would silently weaken the challenge.
static int parse_hex(const char *s, uint8_t *out, size_t n)
{
	if (strlen(s) != 2 * n) {
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		int hi = hexval(s[2 * i]);
		int lo = hexval(s[2 * i + 1]);

		if (hi < 0 || lo < 0) {
			return -1;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

// Assemble + sign the assertion for a challenge nonce under the device key, so any
// holder of the public point can verify it without sharing a secret. That is what
// makes a presence proof portable to a third party (a CI job, a second reviewer)
// rather than only to one paired host.
static int answer_p256(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN],
		       const uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN], int32_t cm)
{
	struct aliro_assert a;
	uint8_t wire[ALIRO_ASSERT_WIRE_P256];

	fill_assert(&a, nonce, cred_id, cm);
	if (!s_dev_set || aliro_assert_build_p256(aliro_assert_ec_sign, &s_dev, &a, wire,
						  sizeof(wire), NULL) != 0) {
		printf("PRESENCE-ERR no usable device signing key\n");
		return 1;
	}
	emit_hex("PRESENCE-P256", wire, sizeof(wire));
	return 0;
}

static bool before_deadline(int64_t deadline_ms)
{
	return woz_uptime_ms() < deadline_ms;
}

static int prove(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	uint8_t expected_pub[ALIRO_ASSERT_PUB_LEN];
	uint8_t expected_id[ALIRO_ASSERT_CREDID_LEN];
	uint8_t actual_pub[ALIRO_ASSERT_PUB_LEN];
	uint8_t actual_id[ALIRO_ASSERT_CREDID_LEN];
	uint32_t auth_checkpoint = 0;
	uint32_t range_checkpoint;
	bool saw_far = false;
	int64_t deadline_ms = woz_uptime_ms() + CONFIG_WOZ_PRESENCE_TIMEOUT_MS;

	if (!aliro_reader_presence_expected_credential(expected_pub)) {
		printf("PRESENCE-ERR proof requires exactly one provisioned credential\n");
		return 1;
	}
	aliro_assert_cred_id(expected_pub, expected_id);

	/* Relock the Wallet edge before ending the old link. The reset itself is
	 * host-task-marshaled by the reader and the checkpoint is published only
	 * after every pre-challenge session has disconnected. */
	notify_wallet(false);
	uint32_t request = aliro_reader_presence_restart();

	while (before_deadline(deadline_ms) &&
	       !aliro_reader_presence_checkpoint(request, &auth_checkpoint)) {
		woz_sleep_ms(PROOF_POLL_MS);
	}
	if (!aliro_reader_presence_checkpoint(request, &auth_checkpoint)) {
		printf("PRESENCE-ERR proof reset timed out\n");
		return 1;
	}

	/* Snapshot after reset completion. A range that races into this tiny gap is
	 * conservatively discarded; that can cost one extra poll, never admit an
	 * old measurement. */
	range_checkpoint = woz_uwb_range_generation();

	while (before_deadline(deadline_ms)) {
		if (aliro_reader_presence_authenticated_after(auth_checkpoint, actual_pub)) {
			aliro_assert_cred_id(actual_pub, actual_id);
			if (memcmp(actual_id, expected_id, sizeof(actual_id)) != 0) {
				printf("PRESENCE-ERR unexpected credential authenticated\n");
				return 1;
			}

			int32_t cm = -1;

			if (woz_uwb_trusted_range_after_cm(&cm, range_checkpoint)) {
				if (cm >= 0 && cm <= CONFIG_WOZ_PRESENCE_MAX_CM) {
					if (answer_p256(nonce, expected_id, cm) != 0) {
						return 1;
					}
					notify_wallet(true);
					return 0;
				}
				saw_far = true;
			}
		}
		woz_sleep_ms(PROOF_POLL_MS);
	}

	if (saw_far) {
		printf("PRESENCE-ERR proof stayed outside %d cm\n",
		       CONFIG_WOZ_PRESENCE_MAX_CM);
	} else {
		printf("PRESENCE-ERR proof timed out; wake the phone and hold it near the reader\n");
	}
	return 1;
}

int presence_link_cmd(int argc, char **argv)
{
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];

	if (argc < 2) {
		printf("PRESENCE-ERR usage: presence pub|credential|prove <nonce-hex>\n");
		return 1;
	}

	if (strcmp(argv[1], "pub") == 0) {
		/* All-zero when keygen or its NVS write failed. Emitted rather than
		 * suppressed so enrolment fails loudly at the host, which knows that no
		 * verifier accepts a frame under an all-zero point. */
		emit_hex("PRESENCE-PUB", s_dev_pub, sizeof(s_dev_pub));
		return 0;
	}

	if (strcmp(argv[1], "credential") == 0) {
		uint8_t cred_pub[ALIRO_ASSERT_PUB_LEN];
		uint8_t cred_id[ALIRO_ASSERT_CREDID_LEN];

		if (!aliro_reader_presence_expected_credential(cred_pub)) {
			printf("PRESENCE-ERR expected exactly one provisioned credential\n");
			return 1;
		}
		aliro_assert_cred_id(cred_pub, cred_id);
		emit_hex("PRESENCE-CRED", cred_id, sizeof(cred_id));
		return 0;
	}

	if (strcmp(argv[1], "prove") == 0) {
		if (argc < 3 || parse_hex(argv[2], nonce, sizeof(nonce)) != 0) {
			printf("PRESENCE-ERR expected a %u-byte nonce as %u hex chars\n",
			       (unsigned)sizeof(nonce), (unsigned)(2 * sizeof(nonce)));
			return 1;
		}
		return prove(nonce);
	}

	printf("PRESENCE-ERR unknown subcommand '%s'\n", argv[1]);
	return 1;
}
