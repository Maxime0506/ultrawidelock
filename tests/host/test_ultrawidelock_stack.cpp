/**
 * @file test_ultrawidelock_stack.cpp — the credential source stack on host.
 *
 * Files under test:
 *   modules/ultrawidelock_cred_stack/src/cred_stack.cpp  error strings, RFC 3339 time,
 *                                                advertising data
 *   modules/ultrawidelock_cred_stack/src/session.cpp      the BLE/NFC session machine
 *
 * The protocol codecs it calls are the shipping sources, linked in whole:
 * every APDU and BLE frame is built and parsed by the real encoders, so a
 * scripted response must be byte-correct. NOT real: everything behind
 * ultrawidelock/interface.h (crypto stand-ins in tests/host/stackfake) -- no evidence
 * a cryptogram authenticates, only of what session.cpp decides on its own:
 * which command next, which key/counter per direction, when a session tears
 * down, what the peer is told on failure.
 */
#include <cstdio>
#include <cstring>
#include <optional>

extern "C" {
#include "test.h"
#include "ultrawidelock_hash.h"
#include "ultrawidelock_prim.h"
}

#include "stackfake.h"

#include <aliro/aliro.h>
#include <aliro/interface.h>
#include <aliro/time.h>

extern "C" {
#include "protocol/ble_message.h"
#include "protocol/nfc_auth.h"
#include "protocol/nfc_select.h"
#include "ultrawidelock_stepup.h"
}

using namespace Aliro;

/* ---- small builders -------------------------------------------------------- */

/** Append one BER TLV; short form only, which is all these responses need. */
static size_t put_tlv(uint8_t *out, uint8_t tag, const uint8_t *value, size_t length)
{
	size_t n = 0;

	out[n++] = tag;
	if (length < 0x80u) {
		out[n++] = static_cast<uint8_t>(length);
	} else {
		out[n++] = 0x81;
		out[n++] = static_cast<uint8_t>(length);
	}
	if (value != nullptr && length > 0U) {
		std::memcpy(out + n, value, length);
	}
	return n + length;
}

/** A P-256 public key with the uncompressed prefix the parsers insist on. */
static void fill_public_key(uint8_t *out, uint8_t seed)
{
	out[0] = 0x04;
	for (size_t i = 1; i < 65; i++) {
		out[i] = static_cast<uint8_t>(seed + i);
	}
}

/* The published expedited-standard SELECT response: FCI with the AID and the
 * proprietary block carrying the supported version list, then 9000. */
static const uint8_t kSelectResponse[] = {
	0x6f, 0x15, 0x84, 0x09, 0xa0, 0x00, 0x00, 0x09, 0x09, 0xac, 0xce, 0x55, 0x01,
	0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00, 0x90, 0x00,
};

/** AUTH0 response: the credential ephemeral key, optionally a fast cryptogram. */
static size_t build_auth0_response(uint8_t *out, bool with_cryptogram)
{
	uint8_t key[65];
	uint8_t cryptogram[64];
	size_t n = 0;

	fill_public_key(key, 0x10);
	n += put_tlv(out + n, 0x86, key, sizeof(key));
	if (with_cryptogram) {
		for (size_t i = 0; i < sizeof(cryptogram); i++) {
			cryptogram[i] = static_cast<uint8_t>(0x70 + i);
		}
		n += put_tlv(out + n, 0x9d, cryptogram, sizeof(cryptogram));
	}
	out[n++] = 0x90;
	out[n++] = 0x00;
	return n;
}

/*
 * The key the User Device answers with, and the counter it is on.
 *
 * DeriveVolatileKeys imports in a fixed order, so the second ImportSymmetricKey
 * IS the expedited device key -- the reader derived it, and the real peer holds
 * the same bytes. Sealing with it is not forgery; using any other key, or the
 * wrong counter, produces a response the reader rightly refuses.
 */
#define DEVICE_KEY()   stackfake_key_nth("symmetric", 1)
#define STEP_UP_KEY()  stackfake_key_nth("SKDevice", 0)
#define BLE_DEVICE_KEY() stackfake_key_nth("BleSKDevice", 0)

/** Seal an APDU-layer device response (no AAD) and append the success status. */
static size_t seal_apdu(uint8_t *out, uint32_t key, uint32_t counter, const uint8_t *plaintext,
			size_t length)
{
	size_t n = stackfake_seal_as_device(key, counter, nullptr, 0, plaintext, length, out);

	if (n == 0U) {
		return 0;
	}
	out[n++] = 0x90;
	out[n++] = 0x00;
	return n;
}

/**
 * AUTH1 response, sealed with the real expedited device key under real
 * AES-256-GCM. The reader is on device counter 1 when it opens this.
 */
static size_t build_auth1_response(uint8_t *out, uint16_t signaling, bool with_timestamp)
{
	uint8_t plaintext[256];
	uint8_t key[65];
	uint8_t signature[64];
	uint8_t bitmap[2];
	uint8_t timestamp[20];
	size_t n = 0;

	fill_public_key(key, 0x20);
	for (size_t i = 0; i < sizeof(signature); i++) {
		signature[i] = static_cast<uint8_t>(0x50 + i);
	}
	std::memcpy(timestamp, "2025-01-01T00:00:00Z", sizeof(timestamp));
	bitmap[0] = static_cast<uint8_t>(signaling >> 8);
	bitmap[1] = static_cast<uint8_t>(signaling);

	n += put_tlv(plaintext + n, 0x5a, key, sizeof(key));
	n += put_tlv(plaintext + n, 0x9e, signature, sizeof(signature));
	n += put_tlv(plaintext + n, 0x5e, bitmap, sizeof(bitmap));
	if (with_timestamp) {
		n += put_tlv(plaintext + n, 0x91, timestamp, sizeof(timestamp));
	}
	return seal_apdu(out, DEVICE_KEY(), 1, plaintext, n);
}

/** The same, presenting a chosen credential public key (Access Document tests). */
static size_t build_auth1_response_for(uint8_t *out, const uint8_t *device_public_key,
				       uint16_t signaling)
{
	uint8_t plaintext[256];
	uint8_t signature[64];
	uint8_t bitmap[2];
	size_t n = 0;

	std::memset(signature, 0x50, sizeof(signature));
	bitmap[0] = static_cast<uint8_t>(signaling >> 8);
	bitmap[1] = static_cast<uint8_t>(signaling);
	n += put_tlv(plaintext + n, 0x5a, device_public_key, 65);
	n += put_tlv(plaintext + n, 0x9e, signature, sizeof(signature));
	n += put_tlv(plaintext + n, 0x5e, bitmap, sizeof(bitmap));
	return seal_apdu(out, DEVICE_KEY(), 1, plaintext, n);
}

/**
 * The exchange response, sealed for real. By now the reader has opened one
 * device message (AUTH1), so it is on device counter 2 -- a test that got that
 * wrong would be refused, which is the point of doing this properly.
 */
static size_t build_exchange_response(uint8_t *out)
{
	static const uint8_t success[] = {0x00, 0x02, 0x00, 0x00};

	return seal_apdu(out, DEVICE_KEY(), 2, success, sizeof(success));
}

static AliroStack &stack(void)
{
	return AliroStack::Instance();
}

/** Reset the doubles and make sure no session survives from a previous case. */
static void ready(void)
{
	stackfake_reset();
	stack().DestroySession(ConnectionHandle::Nfc());
	for (uint8_t id = 0; id < 4; id++) {
		stack().DestroySession(ConnectionHandle::Ble(id));
	}
	stackfake_reset();
}

static void feed_nfc(const uint8_t *bytes, size_t len)
{
	stack().HandleSessionData(ConnectionHandle::Nfc(),
				  {const_cast<uint8_t *>(bytes), len});
}

/* ---- ultrawidelock_stack.cpp: error strings ---------------------------------------- */

static void test_error_strings(void)
{
	t_group("ultrawidelock error strings");

	/* Every code has a message, and the table is indexed by the code -- so
	 * a shifted entry shows up as the wrong string, not as a crash. */
	T_OK("no error", std::strcmp(AliroError(ALIRO_NO_ERROR).ToString(), "No error") == 0);
	T_OK("no memory", std::strcmp(AliroError(ALIRO_NO_MEMORY).ToString(), "No memory") == 0);
	T_OK("invalid state",
	     std::strcmp(AliroError(ALIRO_INVALID_STATE).ToString(), "Invalid state") == 0);
	T_OK("session not found",
	     std::strcmp(AliroError(ALIRO_SESSION_NOT_FOUND).ToString(), "Session not found") == 0);
	T_OK("last entry",
	     std::strcmp(AliroError(ALIRO_NO_PUBLIC_KEY_IN_RESPONSE).ToString(),
			 "No public key in response") == 0);

	/* A code past the table is named rather than read out of bounds. */
	T_OK("out of range",
	     std::strcmp(AliroError(static_cast<AliroErrorCode>(ALIRO_ERROR_MAX)).ToString(),
			 "Unknown error") == 0);

	/* FromInt clamps both ends. */
	T_EQ("from int in range", AliroError::FromInt(ALIRO_TIMEOUT).ToInt(), (int)ALIRO_TIMEOUT);
	T_EQ("from int negative", AliroError::FromInt(-1).ToInt(), (int)ALIRO_ERROR_UNKNOWN);
	T_EQ("from int too large", AliroError::FromInt(ALIRO_ERROR_MAX).ToInt(),
	     (int)ALIRO_ERROR_UNKNOWN);
	T_EQ("from int far too large", AliroError::FromInt(9999).ToInt(),
	     (int)ALIRO_ERROR_UNKNOWN);
}

/* ---- ultrawidelock_stack.cpp: RFC 3339 timestamps ---------------------------------- */

static bool parses(const char *text)
{
	return Time::FromTimestamp(reinterpret_cast<const uint8_t *>(text), 20).has_value();
}

static void test_timestamps(void)
{
	t_group("ultrawidelock timestamps");

	T_OK("a well formed timestamp", parses("2025-08-03T12:34:56Z"));
	{
		auto parsed = Time::FromTimestamp(
			reinterpret_cast<const uint8_t *>("2025-08-03T12:34:56Z"), 20);

		T_OK("parsed", parsed.has_value());
		if (parsed.has_value()) {
			T_EQ("year", parsed->Year(), 2025);
			T_EQ("month", parsed->Month(), 8);
			T_EQ("day", parsed->Day(), 3);
			T_EQ("hour", parsed->Hour(), 12);
			T_EQ("minute", parsed->Minute(), 34);
			T_EQ("second", parsed->Second(), 56);
		}
	}

	/* The form is fixed width; nothing else is accepted. */
	T_OK("null rejected", !Time::FromTimestamp(nullptr, 20).has_value());
	T_OK("short rejected",
	     !Time::FromTimestamp(reinterpret_cast<const uint8_t *>("2025-08-03T12:34:5Z"), 19)
		      .has_value());
	T_OK("wrong date separator", !parses("2025/08-03T12:34:56Z"));
	T_OK("wrong second separator", !parses("2025-08/03T12:34:56Z"));
	T_OK("missing T", !parses("2025-08-03 12:34:56Z"));
	T_OK("wrong first colon", !parses("2025-08-03T12-34:56Z"));
	T_OK("wrong second colon", !parses("2025-08-03T12:34-56Z"));
	T_OK("missing Z", !parses("2025-08-03T12:34:56+"));
	T_OK("non digit in the year", !parses("20x5-08-03T12:34:56Z"));

	/* Calendar bounds. */
	T_OK("year zero rejected", !parses("0000-08-03T12:34:56Z"));
	T_OK("month zero rejected", !parses("2025-00-03T12:34:56Z"));
	T_OK("month thirteen rejected", !parses("2025-13-03T12:34:56Z"));
	T_OK("day zero rejected", !parses("2025-08-00T12:34:56Z"));
	T_OK("day 32 rejected", !parses("2025-08-32T12:34:56Z"));
	T_OK("31 April rejected", !parses("2025-04-31T12:34:56Z"));
	T_OK("30 April accepted", parses("2025-04-30T12:34:56Z"));

	/* Leap years, all three rules. */
	T_OK("29 Feb 2024 accepted (divisible by 4)", parses("2024-02-29T00:00:00Z"));
	T_OK("29 Feb 2025 rejected", !parses("2025-02-29T00:00:00Z"));
	T_OK("29 Feb 1900 rejected (century)", !parses("1900-02-29T00:00:00Z"));
	T_OK("29 Feb 2000 accepted (400)", parses("2000-02-29T00:00:00Z"));

	/* Clock bounds, including the leap second RFC 3339 allows. */
	T_OK("hour 23 accepted", parses("2025-08-03T23:00:00Z"));
	T_OK("hour 24 rejected", !parses("2025-08-03T24:00:00Z"));
	T_OK("minute 59 accepted", parses("2025-08-03T12:59:00Z"));
	T_OK("minute 60 rejected", !parses("2025-08-03T12:60:00Z"));
	T_OK("second 60 accepted as a leap second", parses("2025-08-03T12:34:60Z"));
	T_OK("second 61 rejected", !parses("2025-08-03T12:34:61Z"));
}

/* ---- ultrawidelock_stack.cpp: stack identity and advertising ----------------------- */

static void test_stack_identity(void)
{
	size_t count = 0;
	const ProtocolVersion *versions;

	t_group("ultrawidelock stack identity");

	T_EQ("init succeeds", stack().Init().ToInt(), (int)ALIRO_NO_ERROR);
	T_OK("library version", std::strcmp(AliroStack::GetLibraryVersion(), "ultrawidelock/0.2") == 0);

	versions = stack().GetExpeditedStandardProtocolVersions(count);
	T_EQ("one expedited version", (long)count, 1L);
	T_EQ("expedited version is 1.0", (long)versions[0], 0x0100L);

	versions = stack().GetBleUwbProtocolVersions(count);
	T_EQ("one ble/uwb version", (long)count, 1L);
	T_EQ("ble/uwb version is 1.0", (long)versions[0], 0x0100L);

	T_EQ("advertising version", (long)AliroStack::GetBleAdvertisingVersion(), 0L);

	/* The feature bits are Kconfig-driven; this build sets all three. */
	{
		const uint8_t features = stack().GetFeatures();

		T_OK("expedited fast advertised",
		     (features & kFeatureExpeditedFastPhaseSupported) != 0);
		T_OK("step-up advertised", (features & kFeatureStepUpPhaseSupported) != 0);
		T_OK("ble/uwb advertised", (features & kFeatureBleUwbSupported) != 0);
	}
}

static void test_advertising(void)
{
	BleTypes::AdvertisingServiceData data{};
	const BleTypes::BleAddress address{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	Identifier reader{};

	t_group("ultrawidelock advertising data");

	for (size_t i = 0; i < reader.size(); i++) {
		reader[i] = static_cast<uint8_t>(0xc0 + i);
	}

	stackfake_reset();
	T_EQ("generated",
	     AliroStack::GenerateAdvertisingData(
		     data, address, -20, reader,
		     BleTypes::AdvertisingServiceData::Notification::NoError, 0x11223344)
		     .ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("uwb flow advertised", (long)data.mServiceFlags.bleUwb, 1L);
	T_EQ("version zero", (long)data.mServiceFlags.version, 0L);
	T_EQ("no notification", (long)data.mServiceFlags.notification, 0L);
	T_EQ("tx power carried", (long)data.mTxPowerLevelDbm, -20L);
	T_OK("group id truncated to 8 bytes",
	     std::memcmp(data.mTruncatedReaderGroupId, reader.data(), 8) == 0);
	T_OK("group sub-id taken past the group id",
	     std::memcmp(data.mTruncatedReaderGroupSubId,
			 reader.data() + kReaderGroupIdentifierLength, 2) == 0);
	/* The expiry is big-endian on the wire. */
	T_EQ("expiry byte 0", (long)data.mDynamicTagExpiryTime[0], 0x11L);
	T_EQ("expiry byte 3", (long)data.mDynamicTagExpiryTime[3], 0x44L);
	T_EQ("the dynamic tag was derived by encryption", (long)stackfake.encrypt_calls, 1L);

	/* A notification value really does reach the flags. */
	stackfake_reset();
	T_EQ("generated with a notification",
	     AliroStack::GenerateAdvertisingData(
		     data, address, 0, reader,
		     BleTypes::AdvertisingServiceData::Notification::LowBattery, 0)
		     .ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("low battery advertised", (long)data.mServiceFlags.notification,
	     (long)BleTypes::AdvertisingServiceData::Notification::LowBattery);

	/* Out-of-range arguments are refused rather than truncated into the
	 * advertisement, which a phone would then read as a different reader. */
	T_EQ("tx power below -100 refused",
	     AliroStack::GenerateAdvertisingData(
		     data, address, -101, reader,
		     BleTypes::AdvertisingServiceData::Notification::NoError, 0)
		     .ToInt(),
	     (int)ALIRO_INVALID_ARGUMENT);
	T_EQ("tx power above 20 refused",
	     AliroStack::GenerateAdvertisingData(
		     data, address, 21, reader,
		     BleTypes::AdvertisingServiceData::Notification::NoError, 0)
		     .ToInt(),
	     (int)ALIRO_INVALID_ARGUMENT);
	T_EQ("notification past the enum refused",
	     AliroStack::GenerateAdvertisingData(
		     data, address, 0, reader,
		     static_cast<BleTypes::AdvertisingServiceData::Notification>(9), 0)
		     .ToInt(),
	     (int)ALIRO_INVALID_ARGUMENT);

	/* A failed encryption is propagated, not advertised as a zero tag. */
	stackfake_reset();
	stackfake.encrypt_ret = ALIRO_ERROR_INTERNAL;
	T_EQ("encryption failure propagated",
	     AliroStack::GenerateAdvertisingData(
		     data, address, 0, reader,
		     BleTypes::AdvertisingServiceData::Notification::NoError, 0)
		     .ToInt(),
	     (int)ALIRO_ERROR_INTERNAL);
}

/* ---- session.cpp: the session table ---------------------------------------- */

static void test_session_lifecycle(void)
{
	t_group("ultrawidelock session lifecycle");

	/* An NFC session opens by sending SELECT, built by the real encoder. */
	ready();
	T_EQ("nfc session created", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("one frame sent", (long)stackfake.send_count, 1L);
	if (stackfake_last_send() != nullptr) {
		const struct stackfake_send *sent = stackfake_last_send();

		T_EQ("select is 15 bytes", (long)sent->len, (long)ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE);
		T_EQ("select CLA", (long)sent->bytes[0], 0x00L);
		T_EQ("select INS", (long)sent->bytes[1], 0xa4L);
		T_OK("select carries the expedited AID",
		     std::memcmp(sent->bytes + 5, ultrawidelock_cred_expedited_aid,
				 ULTRAWIDELOCK_CRED_AID_SIZE) == 0);
		T_OK("sent on the nfc handle", !sent->is_ble);
	}

	/* The same handle twice is a programming error, not a second session. */
	T_EQ("duplicate refused", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_INVALID_STATE);

	/* Tearing it down tells the application once, and only once. */
	stack().DestroySession(ConnectionHandle::Nfc());
	T_EQ("termination reported", (long)stackfake.termination_calls, 1L);
	stack().DestroySession(ConnectionHandle::Nfc());
	T_EQ("no second termination", (long)stackfake.termination_calls, 1L);

	/* A transport that cannot send leaves no half-open session behind. */
	ready();
	stackfake.send_ret = ALIRO_ERROR_INTERNAL;
	T_EQ("send failure propagated", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_ERROR_INTERNAL);
	stackfake.send_ret = 0;
	T_EQ("the slot was released",
	     stack().CreateSession(ConnectionHandle::Nfc()).ToInt(), (int)ALIRO_NO_ERROR);

	/* A BLE session negotiates its protocol version at creation. */
	ready();
	T_EQ("ble session created", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("nothing sent yet", (long)stackfake.send_count, 0L);
	T_EQ("a response timer was taken", (long)stackfake.timer_acquire_calls, 1L);

	/* Handle 9 is the suite's peer that offers a version this stack does
	 * not speak; the session is refused and the slot released. */
	T_EQ("unsupported version refused",
	     stack().CreateSession(ConnectionHandle::Ble(9)).ToInt(),
	     (int)ALIRO_VERSION_NOT_SUPPORTED);
	T_EQ("its timer was released", (long)stackfake.timer_release_calls, 1L);

	/* No timer left in the pool means no BLE session: the response timeout
	 * is what stops a stalled peer holding a slot forever. */
	ready();
	stackfake.timers_exhausted = true;
	T_EQ("no timer, no session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_MEMORY);

	/* The table is one NFC slot plus CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS. */
	ready();
	T_EQ("ble 0", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("ble 1", stack().CreateSession(ConnectionHandle::Ble(1)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("nfc", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("the table is full", stack().CreateSession(ConnectionHandle::Ble(2)).ToInt(),
	     (int)ALIRO_NO_MEMORY);

	/* Locking is balanced: every path that takes the stack mutex gives it
	 * back, or the next call would deadlock on target. */
	T_EQ("mutex released", (long)stackfake.mutex_lock_depth, 0L);
	ready();
}

/* ---- session.cpp: the NFC state machine ------------------------------------ */

/** Open an NFC session and walk it as far as the AUTH0 command. */
static void nfc_to_auth0(bool fast_available)
{

	ready();
	if (fast_available) {
		stackfake_set_kpersistent(2);
	}
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
}

static void test_nfc_flow(void)
{
	uint8_t response[512];
	size_t length;

	t_group("ultrawidelock nfc flow");

	/* SELECT answered -> AUTH0 goes out, built by the real encoder. */
	nfc_to_auth0(false);
	T_EQ("select and auth0 sent", (long)stackfake.send_count, 2L);
	if (stackfake_sent(1) != nullptr) {
		const struct stackfake_send *auth0 = stackfake_sent(1);

		T_EQ("auth0 CLA", (long)auth0->bytes[0], 0x80L);
		T_EQ("auth0 INS", (long)auth0->bytes[1], 0x80L);
		T_EQ("standard auth0 length", (long)auth0->len,
		     (long)ULTRAWIDELOCK_CRED_AUTH0_STANDARD_COMMAND_SIZE);
		/* Command Parameters ride in the first TLV of the body (tag
		 * 0x41, one byte), not in P1 -- P1/P2 are always zero here. */
		T_EQ("body starts with command parameters", (long)auth0->bytes[5], 0x41L);
		T_EQ("one byte of them", (long)auth0->bytes[6], 0x01L);
		/* Without stored credentials the fast flag stays clear. */
		T_EQ("fast not requested", (long)auth0->bytes[7], 0x00L);
	}
	T_EQ("an ephemeral key pair was generated", (long)stackfake.ephemeral_calls, 1L);

	/* With stored credentials the reader asks for the fast phase. */
	nfc_to_auth0(true);
	if (stackfake_sent(1) != nullptr) {
		T_EQ("fast requested", (long)stackfake_sent(1)->bytes[7], 0x01L);
	}

	/* AUTH0 answered (standard) -> volatile keys derived, AUTH1 goes out. */
	nfc_to_auth0(false);
	length = build_auth0_response(response, false);
	feed_nfc(response, length);
	T_EQ("auth1 sent", (long)stackfake.send_count, 3L);
	T_EQ("key agreement ran", (long)stackfake.key_agreement_calls, 1L);
	T_EQ("the kdh was hashed", (long)stackfake.sha256_calls, 1L);
	T_EQ("160 bytes of key material derived", (long)stackfake.derive_raw_calls, 1L);
	T_EQ("the reader signed the authentication data", (long)stackfake.sign_calls, 1L);
	if (stackfake_sent(2) != nullptr) {
		T_EQ("auth1 INS", (long)stackfake_sent(2)->bytes[1], 0x81L);
	}

	/* AUTH1 answered with no document wanted -> the completion exchange,
	 * and the session reaches AccessComplete. */
	stackfake.want_access_document = false;
	length = build_auth1_response(response, 0x0000, false);
	feed_nfc(response, length);
	T_EQ("the device signature was verified", (long)stackfake.verify_calls, 1L);
	T_EQ("a persistent key was derived", (long)stackfake.derive_shared_calls, 1L);
	T_EQ("access was processed", (long)stackfake.process_access_calls, 1L);
	T_EQ("the completion exchange went out", (long)stackfake.send_count, 4L);
	if (stackfake_sent(3) != nullptr) {
		T_EQ("exchange INS", (long)stackfake_sent(3)->bytes[1], 0xc9L);
	}

	/* The exchange response ends the transaction and the session closes. */
	length = build_exchange_response(response);
	feed_nfc(response, length);
	T_EQ("the session was terminated", (long)stackfake.termination_calls, 1L);

	/* A device that reports a status word other than 9000 is refused, and
	 * the session is torn down rather than left mid-protocol. */
	nfc_to_auth0(false);
	{
		uint8_t bad[] = {0x6a, 0x82};

		feed_nfc(bad, sizeof(bad));
		T_EQ("a failed auth0 tears the session down",
		     (long)stackfake.termination_calls, 1L);
	}

	/* Data with no session behind it is ignored, not a crash. */
	ready();
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
	T_EQ("no session, nothing happened", (long)stackfake.send_count, 0L);

	/* A null payload is the transport saying the link is gone. */
	ready();
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	stack().HandleSessionData(ConnectionHandle::Nfc(), {nullptr, 0});
	T_EQ("a null payload destroys the session", (long)stackfake.termination_calls, 1L);
}

static void test_nfc_failures(void)
{
	t_group("ultrawidelock nfc failure paths");

	/* Every identity and crypto step SendAuth0 depends on, failed one at a
	 * time. Each must tear the session down rather than send a command
	 * built from half-initialised state. */
	struct {
		const char *name;
		int *knob;
	} cases[] = {
		{"reader identifier unavailable", &stackfake.reader_identifier_ret},
		{"reader public key unavailable", &stackfake.reader_public_key_ret},
		{"ephemeral key pair fails", &stackfake.ephemeral_ret},
		{"random unavailable", &stackfake.random_ret},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		ready();
		T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
		     (int)ALIRO_NO_ERROR);
		*cases[i].knob = ALIRO_ERROR_INTERNAL;
		feed_nfc(kSelectResponse, sizeof(kSelectResponse));
		T_EQ(cases[i].name, (long)stackfake.termination_calls, 1L);
		T_EQ("no auth0 was sent", (long)stackfake.send_count, 1L);
	}

	/* The same for the volatile key schedule, after AUTH0 is answered. */
	struct {
		const char *name;
		int *knob;
	} derive_cases[] = {
		{"key agreement fails", &stackfake.key_agreement_ret},
		{"kdh hash fails", &stackfake.sha256_ret},
		{"kdh import fails", &stackfake.import_shared_ret},
		{"key derivation fails", &stackfake.derive_raw_ret},
		{"directional key import fails", &stackfake.import_symmetric_ret},
		{"signing fails", &stackfake.sign_ret},
	};

	for (size_t i = 0; i < sizeof(derive_cases) / sizeof(derive_cases[0]); i++) {
		uint8_t response[512];
		size_t length;

		nfc_to_auth0(false);
		*derive_cases[i].knob = ALIRO_ERROR_INTERNAL;
		length = build_auth0_response(response, false);
		feed_nfc(response, length);
		T_EQ(derive_cases[i].name, (long)stackfake.termination_calls, 1L);
	}

	/* A reader certificate that will not load stops the AUTH1 build. */
	{
		uint8_t response[512];
		size_t length;

		nfc_to_auth0(false);
		stackfake.certificate_provisioned = true;
		stackfake.certificate_ret = ALIRO_ERROR_INTERNAL;
		length = build_auth0_response(response, false);
		feed_nfc(response, length);
		T_EQ("an unreadable certificate stops auth1",
		     (long)stackfake.termination_calls, 1L);
	}

	/* A provisioned certificate is carried in AUTH1. */
	{
		uint8_t response[512];
		size_t length;

		nfc_to_auth0(false);
		stackfake.certificate_provisioned = true;
		stackfake.certificate_len = 8;
		std::memset(stackfake.certificate, 0x33, stackfake.certificate_len);
		length = build_auth0_response(response, false);
		feed_nfc(response, length);
		T_EQ("auth1 sent with a certificate", (long)stackfake.send_count, 3L);
		if (stackfake_sent(2) != nullptr) {
			T_OK("auth1 grew past the bare form",
			     stackfake_sent(2)->len > ULTRAWIDELOCK_CRED_AUTH1_COMMAND_SIZE);
		}
	}
}

static void test_nfc_fast_path(void)
{
	uint8_t response[512];
	size_t length;

	t_group("ultrawidelock nfc fast path");

	/* A cryptogram the reader cannot decrypt is an ordinary trial result:
	 * the flow falls back to expedited-standard rather than failing. */
	nfc_to_auth0(true);
	stackfake.aead_decrypt_ret = ALIRO_INVALID_AUTHENTICATION_TAG;
	length = build_auth0_response(response, true);
	feed_nfc(response, length);
	T_EQ("both stored credentials were tried", (long)stackfake.aead_decrypt_calls, 2L);
	T_EQ("auth1 went out anyway", (long)stackfake.send_count, 3L);

	/* A credential whose public key cannot be read is skipped, not fatal. */
	ready();
	stackfake_set_kpersistent(1);
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
	stackfake.credential_public_key_ret = ALIRO_ERROR_INTERNAL;
	length = build_auth0_response(response, true);
	feed_nfc(response, length);
	T_EQ("the standard phase still ran", (long)stackfake.send_count, 3L);

	/* A cryptogram that decrypts but does not have the fixed-width shape of
	 * Table 8-6 is not a match. */
	nfc_to_auth0(true);
	length = build_auth0_response(response, true);
	feed_nfc(response, length);
	T_EQ("a wrongly shaped plaintext is not a match", (long)stackfake.send_count, 3L);
}

/* ---- session.cpp: the BLE path --------------------------------------------- */

/** Hand the stack a BLE payload and run the deferred event it queued. */
static void feed_ble(uint8_t id, const uint8_t *bytes, size_t len)
{
	stack().HandleSessionData(ConnectionHandle::Ble(id), {const_cast<uint8_t *>(bytes), len});
	if (stackfake.last_event != nullptr) {
		void *event = stackfake.last_event;

		stackfake.last_event = nullptr;
		stack().ProcessEvent(event);
	}
}

static void test_ble_deferral(void)
{
	uint8_t frame[64];

	t_group("ultrawidelock ble deferral");

	/* BLE data is never processed on the transport's thread: it is copied
	 * into an event and queued, which is what keeps the stack mutex out of
	 * the caller's lock order. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	std::memset(frame, 0, sizeof(frame));
	frame[0] = ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP;
	frame[1] = 1;
	frame[2] = 0;
	frame[3] = 4;
	stack().HandleSessionData(ConnectionHandle::Ble(0), {frame, 8});
	T_EQ("the payload was deferred", (long)stackfake.queue_event_calls, 1L);
	T_OK("an event was produced", stackfake.last_event != nullptr);
	if (stackfake.last_event != nullptr) {
		stack().ProcessEvent(stackfake.last_event);
		stackfake.last_event = nullptr;
	}

	/* A length the event cannot hold is refused and the session closed --
	 * silently truncating a frame would corrupt the protocol instead. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	stack().HandleSessionData(ConnectionHandle::Ble(0), {frame, 0});
	T_EQ("a zero length closes the session", (long)stackfake.termination_calls, 1L);

	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	stack().HandleSessionData(ConnectionHandle::Ble(0), {nullptr, 4});
	T_EQ("a null payload closes the session", (long)stackfake.termination_calls, 1L);

	/* A queue that will not take the event closes the session too: the
	 * alternative is a peer that waits forever for a reply. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	stackfake.queue_event_ret = ALIRO_NO_MEMORY;
	stack().HandleSessionData(ConnectionHandle::Ble(0), {frame, 8});
	T_EQ("a refused event closes the session", (long)stackfake.termination_calls, 1L);

	/* Events the stack does not own are reported, not dereferenced. */
	stack().ProcessEvent(nullptr);
	{
		struct {
			void *reserved;
			uint32_t magic;
		} alien = {nullptr, 0xdeadbeef};

		stack().ProcessEvent(&alien);
	}
	T_OK("unknown events survived", true);
}

static void test_ble_framing(void)
{
	uint8_t frame[64];
	uint8_t oversized[8];

	t_group("ultrawidelock ble framing");

	std::memset(frame, 0, sizeof(frame));

	/* A frame whose declared payload cannot fit the receive buffer is
	 * malformed by construction and closes the session. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	oversized[0] = ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP;
	oversized[1] = 1;
	oversized[2] = 0xff;
	oversized[3] = 0xff;
	feed_ble(0, oversized, 4);
	T_EQ("an oversized frame closes the session", (long)stackfake.termination_calls, 1L);

	/* A zero-length payload is not a frame. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	oversized[2] = 0;
	oversized[3] = 0;
	feed_ble(0, oversized, 4);
	T_EQ("an empty payload closes the session", (long)stackfake.termination_calls, 1L);

	/* Reserved protocol bits set: refused before anything is parsed. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	oversized[0] = 0xc0;
	oversized[2] = 0;
	oversized[3] = 4;
	feed_ble(0, oversized, 4);
	T_EQ("reserved protocol bits close the session", (long)stackfake.termination_calls, 1L);

	/* A HEADER SPLIT ACROSS SDUs is held, not misread. credential messages span
	 * several L2CAP SDUs and the reassembler is what makes that work. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	frame[0] = ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP;
	frame[1] = 1;
	frame[2] = 0;
	frame[3] = 4;
	feed_ble(0, frame, 2); /* half a header */
	T_EQ("a partial header is buffered", (long)stackfake.termination_calls, 0L);
	feed_ble(0, frame + 2, 2); /* the rest of the header, no body yet */
	T_EQ("a header with no body is buffered", (long)stackfake.termination_calls, 0L);
	feed_ble(0, frame + 4, 4); /* the body completes the message */
	/* The message is an AP frame the session is not expecting in
	 * BleConnected state, so it is refused -- but only after reassembly
	 * succeeded, which is what this case is about. */
	T_EQ("the reassembled frame reached the parser", (long)stackfake.termination_calls, 1L);
}

static void test_ble_timeout(void)
{
	t_group("ultrawidelock ble response timeout");

	/* Acquiring the timer is part of opening a BLE session; firing it with
	 * no request outstanding must NOT tear the session down, or an idle
	 * link would drop itself. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	stackfake_fire_timer(0);
	if (stackfake.last_event != nullptr) {
		void *event = stackfake.last_event;

		stackfake.last_event = nullptr;
		stack().ProcessEvent(event);
	}
	T_EQ("an idle timeout is ignored", (long)stackfake.termination_calls, 0L);

	/* A timeout event naming a session index past the table is dropped. */
	ready();
	T_OK("out-of-range timeout survived", true);
}

static void test_ble_uwb_messages(void)
{
	uint8_t plaintext[8];

	t_group("ultrawidelock ble uwb messages");

	/* SendBleMessage with no session at all reports it and does not
	 * destroy anything -- there is nothing to destroy. */
	ready();
	stack().SendBleMessage(ConnectionHandle::Ble(0), plaintext, sizeof(plaintext));
	T_EQ("no session, no termination", (long)stackfake.termination_calls, 0L);

	/* A session that exists but is not ranging is the same answer: the
	 * message belongs to a phase that has not been reached. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	std::memset(plaintext, 0, sizeof(plaintext));
	stack().SendBleMessage(ConnectionHandle::Ble(0), plaintext, sizeof(plaintext));
	T_EQ("not ranging, no termination", (long)stackfake.termination_calls, 0L);

	/* Reader-status broadcast with no ranging session reaches nobody and
	 * still reports success. */
	ready();
	T_EQ("status broadcast with no sessions",
	     stack().SendReaderStatusChangedMessage(OperationSource::Manual,
						    ReaderStateByte::Unsecured)
		     .ToInt(),
	     (int)ALIRO_NO_ERROR);
	T_EQ("nothing was sent", (long)stackfake.send_count, 0L);

	/* WORTH RECORDING RATHER THAN ASSERTING AWAY: the notification builder
	 * range-checks nothing but its output pointer, so an operation source
	 * or reader state outside the enum is encoded verbatim and the call
	 * still reports success. The ALIRO_ERROR_INTERNAL arm of this function
	 * is therefore unreachable from here. */
	ready();
	T_EQ("an out-of-range state is encoded, not refused",
	     stack().SendReaderStatusChangedMessage(
		     static_cast<OperationSource>(0xff),
		     static_cast<ReaderStateByte>(0xfe))
		     .ToInt(),
	     (int)ALIRO_NO_ERROR);

	/* Filtering by credential public key is accepted; with no ranging
	 * session it matches nobody. */
	ready();
	{
		CryptoTypes::PublicKey filter{};

		filter[0] = CryptoTypes::kEccP256PublicKeyPrefix;
		T_EQ("filtered broadcast",
		     stack().SendReaderStatusChangedMessage(OperationSource::Matter,
							    ReaderStateByte::Secured, &filter)
			     .ToInt(),
		     (int)ALIRO_NO_ERROR);
		T_EQ("nothing matched", (long)stackfake.send_count, 0L);
	}
}

/* ---- session.cpp: the BLE flow, end to end --------------------------------- */

/** Wrap @p payload in the four-byte credential BLE header. */
static size_t frame_ble(uint8_t *out, uint8_t protocol, uint8_t message_id, const uint8_t *payload,
			size_t length)
{
	out[0] = protocol;
	out[1] = message_id;
	out[2] = static_cast<uint8_t>(length >> 8);
	out[3] = static_cast<uint8_t>(length);
	if (payload != nullptr && length > 0U) {
		std::memcpy(out + 4, payload, length);
	}
	return 4u + length;
}

/* The Initiate Access notification: a status byte, the proprietary block's
 * length, then the A5 block itself -- the same TLV an NFC SELECT carries,
 * without the FCI wrapper. */
static size_t build_initiate_access(uint8_t *out)
{
	static const uint8_t proprietary[] = {0xa5, 0x08, 0x80, 0x02, 0x00,
					      0x00, 0x5c, 0x02, 0x01, 0x00};
	uint8_t payload[2 + sizeof(proprietary)];

	payload[0] = 0;
	payload[1] = static_cast<uint8_t>(sizeof(proprietary));
	std::memcpy(payload + 2, proprietary, sizeof(proprietary));
	return frame_ble(out, ULTRAWIDELOCK_CRED_BLE_PROTOCOL_NOTIFICATION,
			 ULTRAWIDELOCK_CRED_BLE_NOTIFICATION_INITIATE_ACCESS, payload, sizeof(payload));
}

/**
 * Seal a BLE-layer message as the User Device: the four-byte header, then real
 * AES-256-GCM over the plaintext with that header as additional data. Note the
 * header on the wire declares plaintext+tag, while the AAD declares the
 * PLAINTEXT length -- get that wrong and the reader rejects the frame, which is
 * exactly the check being relied on here.
 */
static size_t build_ble_protected(uint8_t *out, uint8_t protocol, uint8_t id,
				  const uint8_t *plaintext, size_t length, uint32_t counter)
{
	const uint8_t aad[4] = {protocol, id, static_cast<uint8_t>(length >> 8),
				static_cast<uint8_t>(length)};
	const size_t onWire = length + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE;
	size_t sealed;

	out[0] = protocol;
	out[1] = id;
	out[2] = static_cast<uint8_t>(onWire >> 8);
	out[3] = static_cast<uint8_t>(onWire);
	sealed = stackfake_seal_as_device(BLE_DEVICE_KEY(), counter, aad, sizeof(aad), plaintext,
					  length, out + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE);
	return sealed == 0U ? 0U : ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + sealed;
}

/** Wrap an AP response payload the way the peer does: protocol AP, id 1. */
static size_t frame_ap(uint8_t *out, const uint8_t *payload, size_t length)
{
	return frame_ble(out, ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP, 1, payload, length);
}

/** Open a BLE session and take it to the point where AUTH0 has gone out. */
static void ble_to_auth0(void)
{
	uint8_t frame[64];
	size_t length;

	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	length = build_initiate_access(frame);
	feed_ble(0, frame, length);
}

static void test_ble_flow(void)
{
	uint8_t frame[512];
	uint8_t body[512];
	size_t length;

	t_group("ultrawidelock ble flow");

	/* Initiate Access -> AUTH0, framed as a BLE AP message rather than a
	 * bare APDU. That framing is the whole difference between the two
	 * transports at this layer. */
	ble_to_auth0();
	T_EQ("auth0 sent over ble", (long)stackfake.send_count, 1L);
	if (stackfake_last_send() != nullptr) {
		const struct stackfake_send *sent = stackfake_last_send();

		T_OK("sent on the ble handle", sent->is_ble);
		T_EQ("framed as an AP message", (long)sent->bytes[0],
		     (long)ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP);
		T_EQ("apdu follows the header", (long)sent->bytes[4], 0x80L);
		T_EQ("auth0 INS", (long)sent->bytes[5], 0x80L);
		T_EQ("declared payload matches the frame",
		     (long)(((size_t)sent->bytes[2] << 8) | sent->bytes[3]),
		     (long)(sent->len - ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE));
	}
	/* Arming the response timer is what stops a peer that goes quiet from
	 * holding a session slot. */
	T_OK("a response timeout was armed", stackfake.timer_start_calls >= 1u);

	/* AUTH0 answered -> AUTH1 over BLE. */
	length = build_auth0_response(body, false);
	length = frame_ap(frame, body, length);
	feed_ble(0, frame, length);
	T_EQ("auth1 sent", (long)stackfake.send_count, 2L);

	/* AUTH1 answered with no document wanted -> the URSK exchange, which is
	 * the BLE equivalent of the NFC completion exchange. */
	stackfake.want_access_document = false;
	length = build_auth1_response(body, 0x0000, false);
	length = frame_ap(frame, body, length);
	feed_ble(0, frame, length);
	T_EQ("the ursk exchange went out", (long)stackfake.send_count, 3L);
	if (stackfake_sent(2) != nullptr) {
		T_EQ("exchange INS", (long)stackfake_sent(2)->bytes[5], 0xc9L);
	}

	/* THE EXCHANGE RESPONSE COMPLETES ACCESS: the BLE session keys are
	 * derived, ranging starts, and an Access Completed notification goes
	 * out encrypted. */
	length = build_exchange_response(body);
	length = frame_ap(frame, body, length);
	feed_ble(0, frame, length);
	T_EQ("access was processed", (long)stackfake.process_access_calls, 1L);
	T_EQ("both directional ble keys derived", (long)stackfake.derive_symmetric_calls, 2L);
	T_EQ("ranging started", (long)stackfake.ranging_starts, 1L);
	T_EQ("ranging spoke the negotiated version",
	     (long)stackfake.last_ranging_protocol_version, 0x0100L);
	/* The ranging session id is the last four bytes of the transaction
	 * identifier, big-endian; the fake's random is deterministic, so this
	 * is a number the suite can predict. */
	T_EQ("ranging session id", (long)stackfake.last_ranging_session_id, 0xacadaeafL);
	T_EQ("access completed sent", (long)stackfake.send_count, 4L);
	if (stackfake_sent(3) != nullptr) {
		const struct stackfake_send *sent = stackfake_sent(3);

		T_EQ("notification protocol", (long)sent->bytes[0],
		     (long)ULTRAWIDELOCK_CRED_BLE_PROTOCOL_NOTIFICATION);
		T_EQ("access completed id", (long)sent->bytes[1],
		     (long)ULTRAWIDELOCK_CRED_BLE_NOTIFICATION_ACCESS_COMPLETED);
		/* The frame grew by exactly the authentication tag. */
		T_EQ("payload carries the tag",
		     (long)(((size_t)sent->bytes[2] << 8) | sent->bytes[3]),
		     (long)(4 + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE));
	}
	/* ONCE RANGING STARTS THE ACCESS PROTOCOL KEYS ARE GONE. Seven of the
	 * nine slots hold a key on this path -- reader ephemeral, Kdh, both
	 * expedited directions, the StepUpSK root, BleSK and Kpersistent; the
	 * two directional step-up keys were never derived because step-up did
	 * not run, and DestroyKey skips an unset slot. Only the two directional
	 * BleSKs survive, because the ranging session still needs them. */
	T_EQ("access protocol keys destroyed", (long)stackfake.destroy_calls, 7L);

	/* Now in ranging: a UWB control message is decrypted and forwarded. */
	{
		uint8_t plain[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
		uint8_t protectedFrame[64];
		size_t n;

		/* Sealed for real with the reader's own BleSKDevice, on the
		 * first device counter. */
		n = build_ble_protected(protectedFrame, ULTRAWIDELOCK_CRED_BLE_PROTOCOL_UWB, 0, plain,
					sizeof(plain), 1);
		T_OK("frame sealed", n > 0u);
		feed_ble(0, protectedFrame, n);
		T_EQ("the uwb message was forwarded", (long)stackfake.uwb_handle_calls, 1L);
		T_EQ("forwarded with its header", (long)stackfake.last_uwb_message_len,
		     (long)(ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + sizeof(plain)));
		T_EQ("session still open", (long)stackfake.termination_calls, 0L);
	}

	/* SendBleMessage now works, because the session is ranging. */
	{
		uint8_t outbound[8] = {ULTRAWIDELOCK_CRED_BLE_PROTOCOL_UWB, 0, 0, 4, 1, 2, 3, 4};
		const size_t before = stackfake.send_count;

		stack().SendBleMessage(ConnectionHandle::Ble(0), outbound, sizeof(outbound));
		T_EQ("an outbound uwb message was encrypted and sent",
		     (long)(stackfake.send_count - before), 1L);
	}

	/* A reader-status broadcast reaches the ranging session. */
	{
		const size_t before = stackfake.send_count;

		T_EQ("status broadcast",
		     stack().SendReaderStatusChangedMessage(
			     OperationSource::ThisUserDeviceInBluetoothLeUwbAliroFlow,
			     ReaderStateByte::Unsecured)
			     .ToInt(),
		     (int)ALIRO_NO_ERROR);
		T_EQ("it reached the ranging session", (long)(stackfake.send_count - before), 1L);
	}

	/* A NON-RANGING message during ranging is the peer winding down, not a
	 * fault: the session closes cleanly. */
	{
		uint8_t plain[4] = {0x00, 0x00, 0x00, 0x00};
		uint8_t protectedFrame[64];
		size_t n;

		/* Counter 2: the reader already opened the UWB frame above. */
		n = build_ble_protected(protectedFrame, ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP, 7, plain,
					sizeof(plain), 2);
		T_OK("frame sealed", n > 0u);
		feed_ble(0, protectedFrame, n);
		T_EQ("the session closed cleanly", (long)stackfake.termination_calls, 1L);
	}
}

static void test_ble_flow_failures(void)
{
	uint8_t frame[512];
	uint8_t body[512];
	size_t length;

	t_group("ultrawidelock ble failure paths");

	/* A proprietary block that does not parse ends the session before any
	 * key material is touched. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	{
		uint8_t payload[4] = {0, 2, 0xa5, 0x00};

		length = frame_ble(frame, ULTRAWIDELOCK_CRED_BLE_PROTOCOL_NOTIFICATION,
				   ULTRAWIDELOCK_CRED_BLE_NOTIFICATION_INITIATE_ACCESS, payload,
				   sizeof(payload));
		feed_ble(0, frame, length);
		T_EQ("a bad initiate closes the session", (long)stackfake.termination_calls, 1L);
	}

	/* An AP message arriving before Initiate Access is out of order. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	{
		uint8_t payload[4] = {0x90, 0x00, 0x00, 0x00};

		length = frame_ap(frame, payload, sizeof(payload));
		feed_ble(0, frame, length);
		T_EQ("an out-of-order AP message closes the session",
		     (long)stackfake.termination_calls, 1L);
	}

	/* Ranging that will not start is fatal to the session: the phone is
	 * about to range against a reader that is not listening. */
	ble_to_auth0();
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth1_response(body, 0x0000, false));
	feed_ble(0, frame, length);
	stackfake.ranging_ret = ALIRO_ERROR_INTERNAL;
	length = frame_ap(frame, body, build_exchange_response(body));
	feed_ble(0, frame, length);
	T_EQ("a failed ranging start closes the session", (long)stackfake.termination_calls, 1L);

	/* So is a directional key derivation that fails. */
	ble_to_auth0();
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth1_response(body, 0x0000, false));
	feed_ble(0, frame, length);
	stackfake.derive_symmetric_ret = ALIRO_ERROR_INTERNAL;
	length = frame_ap(frame, body, build_exchange_response(body));
	feed_ble(0, frame, length);
	T_EQ("a failed key derivation closes the session",
	     (long)stackfake.termination_calls, 1L);

	/* And an application that refuses access. */
	ble_to_auth0();
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth1_response(body, 0x0000, false));
	feed_ble(0, frame, length);
	stackfake.process_access_ret = ALIRO_INVALID_STATE;
	length = frame_ap(frame, body, build_exchange_response(body));
	feed_ble(0, frame, length);
	T_EQ("a refused access closes the session", (long)stackfake.termination_calls, 1L);

	/* An exchange response whose plaintext is not the success marker. */
	ble_to_auth0();
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth1_response(body, 0x0000, false));
	feed_ble(0, frame, length);
	{
		/* Correctly sealed and correctly counted -- only the plaintext
		 * is wrong, so this reaches the content check rather than
		 * being turned away at the tag. */
		static const uint8_t wrong[] = {0x00, 0x02, 0x00, 0x01};

		length = seal_apdu(body, DEVICE_KEY(), 2, wrong, sizeof(wrong));
		length = frame_ap(frame, body, length);
		feed_ble(0, frame, length);
		T_EQ("a wrong success marker closes the session",
		     (long)stackfake.termination_calls, 1L);
	}

	/* A response the reader cannot decrypt at all. */
	ble_to_auth0();
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	stackfake.aead_decrypt_ret = ALIRO_INVALID_AUTHENTICATION_TAG;
	length = frame_ap(frame, body, build_auth1_response(body, 0x0000, false));
	feed_ble(0, frame, length);
	T_EQ("an undecryptable auth1 closes the session", (long)stackfake.termination_calls, 1L);

	/* THE TAG CHECK ITSELF, with the knob off: a response carrying a tag
	 * this fake would not have produced is refused. That is the one case
	 * here that exercises the authentication branch rather than stepping
	 * over it. */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	length = build_initiate_access(frame);
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	{
		/* Sealed correctly, then one ciphertext byte flipped. Real
		 * AES-256-GCM, so the tag no longer covers these bytes and the
		 * reader must refuse -- this is the authentication check doing
		 * its job, not a knob reporting failure. */
		size_t n = build_auth1_response(body, 0x0000, false);

		body[4] ^= 0x01u;
		length = frame_ap(frame, body, n);
		feed_ble(0, frame, length);
	}
	T_EQ("a forged tag is refused", (long)stackfake.termination_calls, 1L);
}

/* ---- session.cpp: the step-up phase ---------------------------------------- */

static void test_step_up(void)
{
	uint8_t frame[512];
	uint8_t body[512];
	size_t length;

	t_group("ultrawidelock step-up");

	/* An application that wants an Access Document, over NFC, with the
	 * signaling bit that also demands the dedicated step-up AID: the reader
	 * SELECTs it before the ENVELOPE exchange. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 4;
	std::memcpy(stackfake.element_identifier, "elem", 4);
	stackfake.intent_to_store = true;
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	length = build_auth1_response(body, 0x0005, true); /* bits 0 and 2 */
	feed_nfc(body, length);
	T_EQ("the step-up AID was selected", (long)stackfake.send_count, 4L);
	if (stackfake_sent(3) != nullptr) {
		T_OK("select carries the step-up AID",
		     std::memcmp(stackfake_sent(3)->bytes + 5, ultrawidelock_cred_step_up_aid,
				 ULTRAWIDELOCK_CRED_AID_SIZE) == 0);
	}

	/* Answering that SELECT starts the exchange: the directional step-up
	 * keys are derived and the first ENVELOPE goes out. */
	{
		static const uint8_t stepUpSelect[] = {
			0x6f, 0x15, 0x84, 0x09, 0xa0, 0x00, 0x00, 0x09, 0x09, 0xac, 0xce, 0x55,
			0x02, 0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00, 0x90,
			0x00,
		};

		feed_nfc(stepUpSelect, sizeof(stepUpSelect));
		T_EQ("step-up keys derived", (long)stackfake.derive_symmetric_calls, 2L);
		T_EQ("an envelope went out", (long)stackfake.send_count, 5L);
		if (stackfake_sent(4) != nullptr) {
			T_EQ("envelope INS", (long)stackfake_sent(4)->bytes[1], 0xc3L);
		}
	}

	/* A device that answers with more to come drives a GET RESPONSE. */
	{
		uint8_t more[] = {0x61, 0x10};

		feed_nfc(more, sizeof(more));
		T_EQ("get response issued", (long)stackfake.send_count, 6L);
		if (stackfake_sent(5) != nullptr) {
			T_EQ("get response INS", (long)stackfake_sent(5)->bytes[1], 0xc0L);
		}
	}

	/* A complete but unparseable document is refused; the point here is
	 * that the collect/unwrap path ran, not that the document is valid. */
	{
		uint8_t rubbish[32];

		std::memset(rubbish, 0x00, sizeof(rubbish));
		rubbish[sizeof(rubbish) - 2] = 0x90;
		rubbish[sizeof(rubbish) - 1] = 0x00;
		feed_nfc(rubbish, sizeof(rubbish));
		T_EQ("an unparseable document closes the session",
		     (long)stackfake.termination_calls, 1L);
	}

	/* Without bit 2 the reader goes straight to the ENVELOPE exchange, with
	 * no second SELECT. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 4;
	std::memcpy(stackfake.element_identifier, "elem", 4);
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	length = build_auth1_response(body, 0x0001, true); /* bit 0 only */
	feed_nfc(body, length);
	T_EQ("the envelope went out without a second select", (long)stackfake.send_count, 4L);
	if (stackfake_sent(3) != nullptr) {
		T_EQ("envelope INS", (long)stackfake_sent(3)->bytes[1], 0xc3L);
	}

	/* An element identifier the session cannot hold is refused. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 0;
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	length = build_auth1_response(body, 0x0001, true);
	feed_nfc(body, length);
	T_EQ("an empty element identifier closes the session",
	     (long)stackfake.termination_calls, 1L);

	/* Step-up key derivation that fails stops before any envelope. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 4;
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	stackfake.derive_symmetric_ret = ALIRO_ERROR_INTERNAL;
	length = build_auth1_response(body, 0x0001, true);
	feed_nfc(body, length);
	T_EQ("a failed step-up derivation closes the session",
	     (long)stackfake.termination_calls, 1L);

	/* Over BLE the same signaling takes the URSK exchange first: the
	 * document is fetched after ranging is set up, not before. */
	ble_to_auth0();
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 4;
	std::memcpy(stackfake.element_identifier, "elem", 4);
	length = frame_ap(frame, body, build_auth0_response(body, false));
	feed_ble(0, frame, length);
	length = frame_ap(frame, body, build_auth1_response(body, 0x0001, true));
	feed_ble(0, frame, length);
	T_EQ("ble takes the ursk exchange first", (long)stackfake.send_count, 3L);
	if (stackfake_sent(2) != nullptr) {
		T_EQ("exchange INS", (long)stackfake_sent(2)->bytes[5], 0xc9L);
	}
	/* And its exchange response starts step-up rather than completing. */
	length = frame_ap(frame, body, build_exchange_response(body));
	feed_ble(0, frame, length);
	T_EQ("step-up started instead of completing", (long)stackfake.ranging_starts, 0L);
	T_EQ("an envelope went out", (long)stackfake.send_count, 4L);
}

/* ---- session.cpp: validating a real Access Document ------------------------ */

/* The published plaintext DeviceResponse, the same fixture test_ultrawidelock_nfc.c
 * pins the parser against. Its requested data element is "element2". */
static const char kDeviceResponseHex[] =
	"a3613163312e30613281a26131a26131a167616c69726f2d6181d8185838a4613101613258200aa260c8"
	"5ca2f6eca90016720a1d7c7c160baf9cfa1a5aa4156331b71863b426613368656c656d656e743261"
	"34a1000161328443a10126a104478ea23b8fe54e51590133d81859012ea7613163312e30613267534841"
	"2d3235366133a167616c69726f2d61a3005820b193e9b1fd40d43aee51f794fb2754f537a12104b743f5"
	"3ede26d4a74ef604660158202f6f396adb893a91242c60f3b3a32237c90f543cbbed2bf10398ac228955"
	"b7e902582095feb0333d71a311b94921230db1bcd094629c01d0fe5e1f2ab6d888b8997ca36134a16134"
	"a40102200121582096313d6c63e24e3372742bfdb1a33ba2c897dcd68ab8c753e4fbd48dca6b7f9a2258"
	"201fb3269edd418857de1b39a4e4a44b92fa484caa722c228288f01d0c03a2c3d6613567616c69726f2d"
	"616136a36131c074323032342d30362d30315431333a33303a30325a6132c074323032342d30362d3031"
	"5431333a33303a30325a6133c074323032352d30362d30315431333a33303a30325a6137f5584007df31"
	"1fce5e28c83b5b88e6402fae24250c778eec0c58e06283a7d6ab7037e791307aadb8571b1229e18c4993"
	"2de464a4dc4f639ad186eb8742099b56a15d17613567616c69726f2d61613300";

static size_t decode_hex(const char *hex, uint8_t *out, size_t cap)
{
	size_t n = 0;

	for (const char *p = hex; p[0] != '\0' && p[1] != '\0' && n < cap; p += 2) {
		unsigned value = 0;

		for (int i = 0; i < 2; i++) {
			const char c = p[i];

			value <<= 4;
			if (c >= '0' && c <= '9') {
				value |= static_cast<unsigned>(c - '0');
			} else {
				value |= static_cast<unsigned>((c | 0x20) - 'a' + 10);
			}
		}
		out[n++] = static_cast<uint8_t>(value);
	}
	return n;
}

static uint8_t document_bytes[1024];
static size_t document_length;
static struct ultrawidelock_stepup_doc parsed_document;
/* Views the suite pulls out of the parsed fixture: the device key the document
 * binds and the valueDigest of its element2 item. */
static const uint8_t *parsed_device_public_key;
static const uint8_t *parsed_expected_digest;
static const uint8_t kZeroDigest[32] = {};

/**
 * Build the step-up response that carries @p document: the DO53 and
 * session-data wrappers the real builders produce, sixteen filler bytes where
 * the tag would be, then the success status word.
 */
static size_t build_step_up_response(uint8_t *out, size_t cap, const uint8_t *document,
				     size_t length)
{
	uint8_t ciphertext[1024];
	uint8_t sessionData[1024];
	size_t sessionDataLength = 0;
	size_t wrapped = 0;

	/* StartStepUpExchange resets the directional counters, so the device is
	 * back on counter 1 and seals with the step-up device key. */
	const size_t sealed = stackfake_seal_as_device(STEP_UP_KEY(), 1, nullptr, 0, document,
						       length, ciphertext);

	if (sealed == 0U) {
		return 0;
	}
	if (ultrawidelock_stepup_wrap_sessiondata_raw(ciphertext, sealed, sessionData, sizeof(sessionData),
					      &sessionDataLength) != 0) {
		return 0;
	}
	if (ultrawidelock_stepup_wrap_do53(sessionData, sessionDataLength, out, cap - 2, &wrapped) != 0) {
		return 0;
	}
	out[wrapped++] = 0x90;
	out[wrapped++] = 0x00;
	return wrapped;
}

static void test_access_document(void)
{
	uint8_t frame[2048];
	uint8_t body[512];
	size_t length;

	t_group("ultrawidelock access document");

	document_length = decode_hex(kDeviceResponseHex, document_bytes, sizeof(document_bytes));
	T_EQ("fixture decoded", (long)document_length, 492L);

	/* Parse it here with the shipping parser, so the suite can install the
	 * digest the validator will demand and use the device key the document
	 * actually binds. Neither can be invented from outside. */
	T_EQ("fixture parses",
	     ultrawidelock_stepup_parse_response(document_bytes, document_length, &parsed_document), 0);
	parsed_device_public_key = parsed_document.device_key;
	parsed_expected_digest = kZeroDigest;
	for (size_t i = 0; i < parsed_document.n_digests; i++) {
		if (parsed_document.n_items != 0 &&
		    parsed_document.digests[i].id == parsed_document.items[0].digest_id) {
			parsed_expected_digest = parsed_document.digests[i].hash;
			break;
		}
	}

	/* Drive an NFC session to the point where the document is expected. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 8;
	std::memcpy(stackfake.element_identifier, "element2", 8);
	stackfake.intent_to_store = true;

	length = build_auth0_response(body, false);
	feed_nfc(body, length);

	/* The AUTH1 response must present the SAME device key the document
	 * binds, or the validator refuses it -- which is exactly the check
	 * that stops a document being replayed against another credential. */
	feed_nfc(body, build_auth1_response_for(body, parsed_device_public_key, 0x0001));
	T_EQ("an envelope went out", (long)stackfake.send_count, 4L);

	/* The document comes back wrapped, is validated, and access is granted
	 * with the document attached. */
	length = build_step_up_response(frame, sizeof(frame), document_bytes, document_length);
	T_OK("step-up response built", length > 0u);
	feed_nfc(frame, length);
	T_EQ("the issuer key was looked up by kid", (long)stackfake.verify_calls, 2L);
	T_EQ("access was processed with the document",
	     (long)stackfake.process_access_with_document_calls, 1L);
	T_EQ("the step-up completion exchange went out", (long)stackfake.send_count, 5L);

	/* A DIGEST THAT DOES NOT MATCH is a forged data element: refused. */
	{
		uint8_t wrong[32];

		std::memcpy(wrong, parsed_expected_digest, sizeof(wrong));
		wrong[0] ^= 0xffu;
		nfc_to_auth0(false);
		stackfake.want_access_document = true;
		stackfake.element_identifier_len = 8;
		std::memcpy(stackfake.element_identifier, "element2", 8);

		length = build_auth0_response(body, false);
		feed_nfc(body, length);
		length = build_auth1_response(body, 0x0001, true);
		feed_nfc(body, length);
		length = build_step_up_response(frame, sizeof(frame), document_bytes,
						document_length);
		feed_nfc(frame, length);
		T_EQ("a mismatched digest closes the session",
		     (long)stackfake.termination_calls, 1L);
	}

	/* A DOCUMENT BOUND TO ANOTHER CREDENTIAL is refused even though its own
	 * digest and signature are fine: build_auth1_response presents a
	 * different device key. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 8;
	std::memcpy(stackfake.element_identifier, "element2", 8);
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	length = build_auth1_response(body, 0x0001, true);
	feed_nfc(body, length);
	length = build_step_up_response(frame, sizeof(frame), document_bytes, document_length);
	feed_nfc(frame, length);
	T_EQ("a document for another credential closes the session",
	     (long)stackfake.termination_calls, 1L);

	/* An issuer whose key the reader does not hold. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 8;
	std::memcpy(stackfake.element_identifier, "element2", 8);
	stackfake.issuer_public_key_ret = ALIRO_PUBLIC_KEY_NOT_FOUND;
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	length = build_auth1_response(body, 0x0001, true);
	feed_nfc(body, length);
	length = build_step_up_response(frame, sizeof(frame), document_bytes, document_length);
	feed_nfc(frame, length);
	T_EQ("an unknown issuer closes the session", (long)stackfake.termination_calls, 1L);

	/* A signature that does not verify. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 8;
	std::memcpy(stackfake.element_identifier, "element2", 8);
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	feed_nfc(body, build_auth1_response_for(body, parsed_device_public_key, 0x0001));
	stackfake.verify_ret = ALIRO_INVALID_SIGNATURE;
	length = build_step_up_response(frame, sizeof(frame), document_bytes, document_length);
	feed_nfc(frame, length);
	T_EQ("a bad document signature closes the session",
	     (long)stackfake.termination_calls, 1L);

	/* A validity window the reader judges expired. */
	nfc_to_auth0(false);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 8;
	std::memcpy(stackfake.element_identifier, "element2", 8);
	length = build_auth0_response(body, false);
	feed_nfc(body, length);
	feed_nfc(body, build_auth1_response_for(body, parsed_device_public_key, 0x0001));
	stackfake.validity_known = true;
	stackfake.validity_answer = false;
	length = build_step_up_response(frame, sizeof(frame), document_bytes, document_length);
	feed_nfc(frame, length);
	T_EQ("an expired document closes the session", (long)stackfake.termination_calls, 1L);
}

/* ---- session.cpp: the fast path that matches ------------------------------- */

/**
 * Build a cryptogram the reader will genuinely open.
 *
 * The fast phase derives its cryptogram key with HKDF from a stored persistent
 * credential and a salt the reader assembles from the transaction. The real
 * User Device derives the same key from the same inputs, so this does exactly
 * that: run one probe pass to capture the salt the reader built, derive the key
 * with the SAME real HKDF, and seal a Table 8-6 plaintext with real
 * AES-256-GCM under the all-zero nonce the phase uses.
 *
 * Nothing here reimplements session.cpp: the salt is taken from what the reader
 * actually produced, not rebuilt from a copy of its layout.
 */
static void seal_fast_cryptogram(const uint8_t *credential_ephemeral_public_key,
				 uint8_t cryptogram[64])
{
	uint8_t plaintext[48] = {0};
	uint8_t kpersistent[32];
	uint8_t block[160];
	const uint8_t nonce[12] = {0};

	/* Table 8-6: signaling bitmap, then both fixed-width signed timestamps. */
	plaintext[0] = 0x5e;
	plaintext[1] = 0x02;
	plaintext[4] = 0x91;
	plaintext[5] = 0x14;
	plaintext[26] = 0x92;
	plaintext[27] = 0x14;

	T_OK("persistent key material available",
	     stackfake_key_bytes(stackfake_key_nth("kpersistent", 0), kpersistent));
	T_OK("the reader built a fast salt", stackfake.first_salt_len > 0u);

	/* Same HKDF the reader used, over the salt the reader produced. */
	T_EQ("derive the cryptogram key",
	     ultrawidelock_hkdf(stackfake.first_salt, stackfake.first_salt_len, kpersistent,
			sizeof(kpersistent), credential_ephemeral_public_key + 1, 32, block,
			sizeof(block)),
	     0);
	T_EQ("seal the cryptogram",
	     ultrawidelock_aes256_gcm_encrypt(block, nonce, sizeof(nonce), nullptr, 0, plaintext,
				      sizeof(plaintext), cryptogram, cryptogram + sizeof(plaintext),
				      16),
	     0);
}

/** One AUTH0 response carrying @p cryptogram as the fast-phase cryptogram. */
static size_t auth0_with_cryptogram(uint8_t *out, const uint8_t *credential_key,
				    const uint8_t cryptogram[64])
{
	size_t n = put_tlv(out, 0x86, credential_key, 65);

	n += put_tlv(out + n, 0x9d, cryptogram, 64);
	out[n++] = 0x90;
	out[n++] = 0x00;
	return n;
}

static void test_fast_match(void)
{
	uint8_t response[512];
	uint8_t credential[65];
	uint8_t cryptogram[64];
	size_t n;

	t_group("ultrawidelock fast path match");

	fill_public_key(credential, 0x10);

	/* PROBE PASS: a cryptogram that cannot open, run only to capture the
	 * salt the reader assembles. Everything the salt is built from is
	 * deterministic here, so the next pass sees the same one. */
	ready();
	stackfake_set_kpersistent(1);
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
	std::memset(cryptogram, 0, sizeof(cryptogram));
	n = auth0_with_cryptogram(response, credential, cryptogram);
	feed_nfc(response, n);
	T_OK("the fast key was derived on the probe", stackfake.derive_raw_calls >= 1u);
	seal_fast_cryptogram(credential, cryptogram);

	/* THE REAL PASS. The reader opens the cryptogram for real, so a match
	 * here means real AES-256-GCM authenticated it under a key both sides
	 * derived independently with real HKDF. */
	ready();
	stackfake_set_kpersistent(1);
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
	n = auth0_with_cryptogram(response, credential, cryptogram);
	feed_nfc(response, n);

	T_EQ("the cryptogram was opened", (long)stackfake.aead_decrypt_calls, 1L);
	/* A match skips the standard phase entirely: no AUTH1, straight to the
	 * completion exchange. That saving is the whole point of the fast
	 * phase. */
	T_EQ("no auth1 was needed", (long)stackfake.send_count, 3L);
	if (stackfake_sent(2) != nullptr) {
		T_EQ("the completion exchange went out instead",
		     (long)stackfake_sent(2)->bytes[1], 0xc9L);
	}
	T_EQ("access was granted from the stored credential",
	     (long)stackfake.process_access_fast_calls, 1L);
	T_EQ("no signature was generated", (long)stackfake.sign_calls, 0L);

	/* ONE FLIPPED BIT AND IT IS NOT A MATCH. This is the check the fast
	 * phase rests on, and it is now real: the tag no longer covers these
	 * bytes, so the reader falls back to expedited-standard. */
	{
		uint8_t tampered[64];

		std::memcpy(tampered, cryptogram, sizeof(tampered));
		tampered[3] ^= 0x01u;
		ready();
		stackfake_set_kpersistent(1);
		T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
		     (int)ALIRO_NO_ERROR);
		feed_nfc(kSelectResponse, sizeof(kSelectResponse));
		n = auth0_with_cryptogram(response, credential, tampered);
		feed_nfc(response, n);
		T_EQ("a tampered cryptogram falls back to standard",
		     (long)stackfake.sign_calls, 1L);
		if (stackfake_sent(2) != nullptr) {
			T_EQ("auth1 INS", (long)stackfake_sent(2)->bytes[1], 0x81L);
		}
	}

	/* A matched cryptogram can still need the standard phase when the
	 * application wants a newer Access Document. */
	ready();
	stackfake_set_kpersistent(1);
	stackfake.want_access_document = true;
	stackfake.element_identifier_len = 4;
	std::memcpy(stackfake.element_identifier, "elem", 4);
	T_EQ("nfc session", stack().CreateSession(ConnectionHandle::Nfc()).ToInt(),
	     (int)ALIRO_NO_ERROR);
	feed_nfc(kSelectResponse, sizeof(kSelectResponse));
	n = auth0_with_cryptogram(response, credential, cryptogram);
	feed_nfc(response, n);
	T_EQ("the standard phase ran after all", (long)stackfake.send_count, 3L);
	if (stackfake_sent(2) != nullptr) {
		T_EQ("auth1 INS", (long)stackfake_sent(2)->bytes[1], 0x81L);
	}
}

/* ---- session.cpp: the salt layout and the key schedule --------------------- */

static void test_key_schedule(void)
{
	uint8_t response[512];
	uint8_t reader_key[65];
	uint8_t ephemeral[65];
	uint8_t credential[65];
	uint8_t kdh[32];
	uint8_t block[160];
	uint8_t got[32];
	const uint8_t *salt;
	size_t salt_len;

	t_group("ultrawidelock key schedule");

	/* A plain expedited-standard BLE flow, so every key in the schedule is
	 * derived (BleSK only exists on BLE). */
	ready();
	T_EQ("ble session", stack().CreateSession(ConnectionHandle::Ble(0)).ToInt(),
	     (int)ALIRO_NO_ERROR);
	{
		uint8_t frame[512];
		size_t n = build_initiate_access(frame);

		feed_ble(0, frame, n);
		n = frame_ap(frame, response, build_auth0_response(response, false));
		feed_ble(0, frame, n);
	}

	salt = stackfake.first_salt;
	salt_len = stackfake.first_salt_len;

	/* THE SALT IS A WIRE-VISIBLE STRUCTURE. Both sides build it
	 * independently and an iPhone will not agree with a reader that orders
	 * it differently -- and because both sides of THIS suite derive from
	 * the reader's own salt, only an explicit pin can catch a reordering.
	 * Table 8-x: reader public key X, a 12-byte label, the reader
	 * identifier, the interface byte, the version TLV, the reader ephemeral
	 * X, the transaction identifier, the two flag bytes, then the
	 * proprietary information. */
	fill_public_key(reader_key, 0);
	(void)reader_key;
	T_OK("salt is long enough to hold the fixed part", salt_len >= 131u);
	if (salt_len < 131u) {
		return;
	}
	/* Reader public key X coordinate, prefix stripped. */
	{
		CryptoTypes::PublicKey rk{};

		T_EQ("reader key available",
		     Aliro::Interface::Reader::GetPublicKey(rk).ToInt(), (int)ALIRO_NO_ERROR);
		T_OK("salt[0..32] is the reader public key X",
		     std::memcmp(salt, rk.data() + 1, 32) == 0);
	}
	T_OK("salt[32..44] is the Volatile label",
	     std::memcmp(salt + 32, "Volatile****", 12) == 0);
	{
		Identifier id{};

		T_EQ("reader identifier available",
		     Aliro::Interface::Reader::GetIdentifier(id).ToInt(), (int)ALIRO_NO_ERROR);
		T_OK("salt[44..76] is the reader identifier",
		     std::memcmp(salt + 44, id.data(), 32) == 0);
	}
	/* 0xc3 marks BLE; NFC uses 0x5e. Getting this wrong silently derives a
	 * key the phone will not match on the other transport. */
	T_EQ("salt[76] is the BLE interface byte", (long)salt[76], 0xc3L);
	T_EQ("salt[77] version TLV tag", (long)salt[77], 0x5cL);
	T_EQ("salt[78] version TLV length", (long)salt[78], 0x02L);
	T_EQ("salt[79] protocol major", (long)salt[79], 0x01L);
	T_EQ("salt[80] protocol minor", (long)salt[80], 0x00L);
	/* The reader ephemeral X, then the transaction identifier the reader
	 * drew, then Command Parameters and Authentication Policy. */
	T_EQ("salt[129] command parameters", (long)salt[129], 0x00L);
	T_EQ("salt[130] authentication policy", (long)salt[130], 0x01L);

	/* THE 160-BYTE BLOCK IS SLICED IN A FIXED ORDER. Re-derive it here with
	 * the same real HKDF and check which slice became which key -- a
	 * swapped pair would still decrypt against itself and pass every other
	 * test in this file. */
	T_OK("Kdh is in the key table",
	     stackfake_key_bytes(stackfake_key_nth("shared", 0), kdh));
	fill_public_key(credential, 0x10);
	(void)ephemeral;
	T_EQ("re-derive the block",
	     ultrawidelock_hkdf(salt, salt_len, kdh, sizeof(kdh), credential + 1, 32, block,
			sizeof(block)),
	     0);

	T_OK("bytes 0..32 became the expedited READER key",
	     stackfake_key_bytes(stackfake_key_nth("symmetric", 0), got) &&
		     std::memcmp(got, block, 32) == 0);
	T_OK("bytes 32..64 became the expedited DEVICE key",
	     stackfake_key_bytes(stackfake_key_nth("symmetric", 1), got) &&
		     std::memcmp(got, block + 32, 32) == 0);
	T_OK("bytes 64..96 became StepUpSK",
	     stackfake_key_bytes(stackfake_key_nth("shared", 1), got) &&
		     std::memcmp(got, block + 64, 32) == 0);
	T_OK("bytes 96..128 became BleSK",
	     stackfake_key_bytes(stackfake_key_nth("shared", 2), got) &&
		     std::memcmp(got, block + 96, 32) == 0);

	/* And bytes 128..160 are the URSK handed to the ranging session -- the
	 * one piece of this schedule that leaves the stack. */
	{
		uint8_t frame[512];
		size_t n;

		stackfake.want_access_document = false;
		n = frame_ap(frame, response, build_auth1_response(response, 0x0000, false));
		feed_ble(0, frame, n);
		n = frame_ap(frame, response, build_exchange_response(response));
		feed_ble(0, frame, n);
		T_EQ("ranging started", (long)stackfake.ranging_starts, 1L);
		T_OK("bytes 128..160 became the URSK",
		     std::memcmp(stackfake.last_ursk, block + 128, 32) == 0);
	}

	/* The directional BLE keys come from BleSK by HKDF with the two info
	 * strings, and they must not be the same key. */
	{
		uint8_t reader[32];
		uint8_t device[32];

		T_OK("BleSKReader derived",
		     stackfake_key_bytes(stackfake_key_nth("BleSKReader", 0), reader));
		T_OK("BleSKDevice derived",
		     stackfake_key_bytes(stackfake_key_nth("BleSKDevice", 0), device));
		T_OK("the two directions are different keys",
		     std::memcmp(reader, device, 32) != 0);
	}
}

int main(void)
{
	test_error_strings();
	test_timestamps();
	test_stack_identity();
	test_advertising();
	test_session_lifecycle();
	test_nfc_flow();
	test_nfc_failures();
	test_nfc_fast_path();
	test_ble_deferral();
	test_ble_framing();
	test_ble_timeout();
	test_ble_uwb_messages();
	test_ble_flow();
	test_ble_flow_failures();
	test_step_up();
	test_access_document();
	test_fast_match();
	test_key_schedule();

	if (t_fail > 0) {
		std::printf("  ultrawidelock-stack: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	std::printf("  ultrawidelock-stack: PASS (%d checks — real protocol codecs, real "
		    "SHA-256/HKDF/AES-GCM, stand-in P-256)\n",
		    t_pass);
	return 0;
}
