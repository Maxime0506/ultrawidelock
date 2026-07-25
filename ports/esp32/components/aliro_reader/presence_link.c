// Presence dongle commands (see presence_link.h). Latches the latest trusted range
// via the facade's range listener, and answers each nonce with a signed assertion of
// the current presence state. Two signing paths: an HMAC under the NVS pairing key,
// verifiable only by the paired host, and ECDSA-P256 under a per-device key
// generated on first boot, verifiable by anyone holding the public point.
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

#include "esp_timer.h"
#include "nvs.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"
#include "aliro_assert.h"
#include "aliro_assert_ec.h"
#include "aliro_prim.h"
#include "presence_link.h"

#define PRESENCE_NS     "presence"
#define PRESENCE_KEY    "kpair"
#define PRESENCE_DEVKEY "kdev"
/* A range must have latched within this window for the dongle to call it PRESENT;
 * otherwise the phone has walked away and presence has gone stale. */
#define RANGE_FRESH_MS 5000

static bool s_drive_wallet;
static uint8_t s_key[ALIRO_ASSERT_KEY_LEN];
static bool s_key_set;

/* Device signing identity. Only the private scalar is persisted; the public point
 * is re-derived at every boot so the two can never drift apart in NVS. */
static struct aliro_assert_ec_priv s_dev;
static uint8_t s_dev_pub[ALIRO_ASSERT_PUB_LEN];
static bool s_dev_set;

static void load_key(void)
{
	nvs_handle_t h;

	if (nvs_open(PRESENCE_NS, NVS_READONLY, &h) != ESP_OK) {
		return;
	}
	size_t len = sizeof(s_key);
	if (nvs_get_blob(h, PRESENCE_KEY, s_key, &len) == ESP_OK && len == sizeof(s_key)) {
		s_key_set = true;
	}
	nvs_close(h);
}

int presence_link_set_key(const uint8_t key[32])
{
	nvs_handle_t h;

	if (nvs_open(PRESENCE_NS, NVS_READWRITE, &h) != ESP_OK) {
		return -1;
	}
	esp_err_t e = nvs_set_blob(h, PRESENCE_KEY, key, ALIRO_ASSERT_KEY_LEN);

	if (e == ESP_OK) {
		e = nvs_commit(h);
	}
	nvs_close(h);
	if (e != ESP_OK) {
		return -1;
	}
	memcpy(s_key, key, ALIRO_ASSERT_KEY_LEN);
	s_key_set = true;
	return 0;
}

// Load the device signing key from NVS, generating and persisting one on first
// boot. This key IS the dongle's identity to every third-party verifier, so it has
// to outlive reboots: a key regenerated each boot would silently invalidate every
// public key anyone had already enrolled.
static void load_or_make_dev_key(void)
{
	nvs_handle_t h;
	uint8_t priv[ALIRO_ASSERT_KEY_LEN];
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
	load_key();
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

// Fill in the assertion body both signing paths share: the challenge nonce plus
// the current presence verdict from the range latch and the authenticated
// credential. Signing is the caller's job, so the two modes state identical facts.
// Also drives the Wallet grant/relock edge, so no signing path can forget it.
static void fill_assert(struct aliro_assert *a, const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	memset(a, 0, sizeof(*a));
	memcpy(a->nonce, nonce, ALIRO_ASSERT_NONCE_LEN);
	a->uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
	/* unix_ms stays ALIRO_ASSERT_TIME_NONE (the memset above): this dongle has
	 * no trusted wall clock, and claiming one it cannot back would be worse
	 * than admitting it has none. Freshness therefore rests entirely on the
	 * verifier's nonce being unpredictable -- which holds for a third-party
	 * verifier of a P-256 frame exactly as it does for the paired host. */

	uint8_t cred_pub[65];
	int32_t cm = -1;
	int64_t age_ms = 0;
	bool fresh = woz_uwb_trusted_range_age_cm(&cm, &age_ms) && cm >= 0 &&
		     age_ms <= RANGE_FRESH_MS;
	bool have_cred = aliro_reader_authenticated_credential(cred_pub);

	if (fresh && have_cred) {
		a->status = ALIRO_PRESENCE_PRESENT;
		a->distance_cm = (cm > 0xFFFE) ? 0xFFFEu : (uint16_t)cm;
		aliro_assert_cred_id(cred_pub, a->cred_id);
	} else {
		a->status = ALIRO_PRESENCE_ABSENT;
		a->distance_cm = ALIRO_ASSERT_DIST_NONE;
	}
	notify_wallet(fresh && have_cred);
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

// Assemble + HMAC the assertion for a challenge nonce. Only the paired host, holding
// the same secret, can check this. With no pairing key yet, sign under an all-zero
// key so the host's check fails cleanly (a clean deny) rather than the dongle going
// silent and the host blocking on a response that never comes.
static void answer_hmac(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	static const uint8_t zero_key[ALIRO_ASSERT_KEY_LEN] = { 0 };
	struct aliro_assert a;
	uint8_t wire[ALIRO_ASSERT_WIRE_HMAC];

	fill_assert(&a, nonce);
	aliro_assert_build(s_key_set ? s_key : zero_key, &a, wire, sizeof(wire), NULL);
	emit_hex("PRESENCE-HMAC", wire, sizeof(wire));
}

// As above but signed under the device key, so any holder of the public point can
// verify it without sharing a secret. This is what makes a presence proof portable
// to a third party (a CI job, a second reviewer) rather than only to this host.
static void answer_p256(const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN])
{
	struct aliro_assert a;
	uint8_t wire[ALIRO_ASSERT_WIRE_P256];

	fill_assert(&a, nonce);
	if (!s_dev_set || aliro_assert_build_p256(aliro_assert_ec_sign, &s_dev, &a, wire,
						  sizeof(wire), NULL) != 0) {
		printf("PRESENCE-ERR no usable device signing key\n");
		return;
	}
	emit_hex("PRESENCE-P256", wire, sizeof(wire));
}

int presence_link_cmd(int argc, char **argv)
{
	uint8_t nonce[ALIRO_ASSERT_NONCE_LEN];

	if (argc < 2) {
		printf("PRESENCE-ERR usage: presence pub|assert <nonce-hex>|hmac <nonce-hex>|"
		       "key <hex>\n");
		return 1;
	}

	if (strcmp(argv[1], "pub") == 0) {
		/* All-zero when keygen or its NVS write failed. Emitted rather than
		 * suppressed so enrolment fails loudly at the host, which knows that no
		 * verifier accepts a frame under an all-zero point. */
		emit_hex("PRESENCE-PUB", s_dev_pub, sizeof(s_dev_pub));
		return 0;
	}

	if (strcmp(argv[1], "assert") == 0 || strcmp(argv[1], "hmac") == 0) {
		if (argc < 3 || parse_hex(argv[2], nonce, sizeof(nonce)) != 0) {
			printf("PRESENCE-ERR expected a %u-byte nonce as %u hex chars\n",
			       (unsigned)sizeof(nonce), (unsigned)(2 * sizeof(nonce)));
			return 1;
		}
		if (argv[1][0] == 'a') {
			answer_p256(nonce);
		} else {
			answer_hmac(nonce);
		}
		return 0;
	}

	if (strcmp(argv[1], "key") == 0) {
		uint8_t key[ALIRO_ASSERT_KEY_LEN];

		if (argc < 3 || parse_hex(argv[2], key, sizeof(key)) != 0) {
			printf("PRESENCE-ERR expected a %u-byte key as %u hex chars\n",
			       (unsigned)sizeof(key), (unsigned)(2 * sizeof(key)));
			return 1;
		}
		if (presence_link_set_key(key) != 0) {
			printf("PRESENCE-ERR could not persist the pairing key\n");
			return 1;
		}
		printf("PRESENCE-KEY ok\n");
		return 0;
	}

	printf("PRESENCE-ERR unknown subcommand '%s'\n", argv[1]);
	return 1;
}
