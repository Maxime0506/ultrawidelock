/* stackfake — the Aliro Interface as recording doubles. See stackfake.h for
 * what this is and is not evidence of. */

#include "stackfake.h"

#include <cstring>

struct stackfake_state stackfake;

static uint32_t kpersistent_ids[STACKFAKE_MAX_KPERSIST];
static size_t kpersistent_id_count;

/* Count a knob down. -1 never fires; 0 fires now and stays fired. */
static bool knob_fires(int *counter)
{
	if (*counter < 0) {
		return false;
	}
	if (*counter == 0) {
		return true;
	}
	--*counter;
	return false;
}

void stackfake_reset(void)
{
	std::memset(&stackfake, 0, sizeof(stackfake));
	stackfake.send_fail_in = -1;
	stackfake.import_symmetric_fail_in = -1;
	stackfake.import_shared_fail_in = -1;
	stackfake.derive_symmetric_fail_in = -1;
	stackfake.next_key_id = 0x100;
	stackfake.validity_known = true;
	stackfake.validity_answer = true;
	kpersistent_id_count = 0;
}

const struct stackfake_send *stackfake_sent(size_t index)
{
	return index < stackfake.send_count ? &stackfake.sends[index] : nullptr;
}

const struct stackfake_send *stackfake_last_send(void)
{
	return stackfake.send_count > 0U ? &stackfake.sends[stackfake.send_count - 1U] : nullptr;
}

void stackfake_set_kpersistent(const uint32_t *ids, size_t count)
{
	if (count > STACKFAKE_MAX_KPERSIST) {
		count = STACKFAKE_MAX_KPERSIST;
	}
	if (ids != nullptr && count > 0U) {
		std::memcpy(kpersistent_ids, ids, count * sizeof(*ids));
	}
	kpersistent_id_count = (ids != nullptr) ? count : 0U;
	stackfake.kpersistent_count = kpersistent_id_count;
}

void stackfake_fire_timer(int handle)
{
	if (handle < 0 || handle >= STACKFAKE_MAX_TIMERS) {
		return;
	}
	struct stackfake_timer *timer = &stackfake.timers[handle];

	if (!timer->acquired || timer->callback == nullptr) {
		return;
	}
	/* A real one-shot has stopped by the time its callback runs, and
	 * ProcessResponseTimeout checks exactly that. */
	timer->running = false;
	timer->callback(timer->context);
}

/* The fake's authentication tag: arithmetic over the key id and nonce, so the
 * fake can recognise its own ciphertext and reject anything else. Not a MAC. */
void stackfake_tag(uint32_t key_id, const uint8_t *nonce, uint8_t *tag_out)
{
	for (size_t i = 0; i < Aliro::CryptoTypes::kAuthenticationTagLength; i++) {
		uint8_t n = (nonce != nullptr) ? nonce[i % Aliro::CryptoTypes::kNonceLength] : 0u;

		tag_out[i] = static_cast<uint8_t>((key_id >> ((i % 4u) * 8u)) ^ n ^ (0x5au + i));
	}
}

namespace Aliro::Interface
{

/* ---- Access ---------------------------------------------------------------- */

namespace Access
{

std::optional<AccessDocumentRequestParams>
GetAccessDocumentRequestParameters(const CryptoTypes::PublicKey &publicKey,
				   const std::optional<Timestamp> &credentialSignedTimestamp)
{
	(void)publicKey;
	(void)credentialSignedTimestamp;
	if (!stackfake.want_access_document) {
		return std::nullopt;
	}
	AccessDocumentRequestParams params{};

	params.mElementIdentifier.mData = stackfake.element_identifier;
	params.mElementIdentifier.mLength = stackfake.element_identifier_len;
	params.mIntentToStore = stackfake.intent_to_store;
	return params;
}

AliroError ProcessAccessRequest(ConnectionHandle handle, const CryptoTypes::PublicKey &userPublicKey,
				CryptoTypes::KeyId kpersistentKeyId,
				const std::optional<AccessDocumentTypes::AccessDocument> &accessDocument)
{
	(void)handle;
	(void)userPublicKey;
	(void)kpersistentKeyId;
	stackfake.process_access_calls++;
	if (accessDocument.has_value()) {
		stackfake.process_access_with_document_calls++;
	}
	return static_cast<AliroErrorCode>(stackfake.process_access_ret);
}

AliroError ProcessAccessRequest(ConnectionHandle handle, CryptoTypes::KeyId kpersistentKeyId)
{
	(void)handle;
	(void)kpersistentKeyId;
	stackfake.process_access_calls++;
	stackfake.process_access_fast_calls++;
	return static_cast<AliroErrorCode>(stackfake.process_access_ret);
}

AliroError GetCredentialIssuerPublicKey(const CryptoTypes::KeyIdentifier &keyIdentifier,
					CryptoTypes::PublicKey &publicKey)
{
	(void)keyIdentifier;
	if (stackfake.issuer_public_key_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.issuer_public_key_ret);
	}
	publicKey.fill(0);
	publicKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	publicKey[1] = 0x11; /* the issuer key a suite's document is signed by */
	return ALIRO_NO_ERROR;
}

AliroError GetKpersistentCount(size_t &count)
{
	count = stackfake.kpersistent_count;
	return static_cast<AliroErrorCode>(stackfake.kpersistent_count_ret);
}

AliroError GetKpersistentKeyIds(CryptoTypes::KeyId *keyIds, size_t &count)
{
	if (stackfake.kpersistent_ids_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.kpersistent_ids_ret);
	}
	if (count > kpersistent_id_count) {
		count = kpersistent_id_count;
	}
	for (size_t i = 0; i < count; i++) {
		keyIds[i] = kpersistent_ids[i];
	}
	return ALIRO_NO_ERROR;
}

AliroError GetAccessCredentialPublicKey(CryptoTypes::KeyId kpersistentKeyId,
					CryptoTypes::PublicKey &publicKey)
{
	if (stackfake.credential_public_key_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.credential_public_key_ret);
	}
	publicKey.fill(0);
	publicKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	/* Distinct per credential, so a suite can tell which one matched. */
	publicKey[1] = static_cast<uint8_t>(kpersistentKeyId);
	return ALIRO_NO_ERROR;
}

} // namespace Access

/* ---- Reader ---------------------------------------------------------------- */

namespace Reader
{

AliroError GetIdentifier(Identifier &identifier)
{
	if (stackfake.reader_identifier_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.reader_identifier_ret);
	}
	for (size_t i = 0; i < identifier.size(); i++) {
		identifier[i] = static_cast<uint8_t>(0x40 + i);
	}
	return ALIRO_NO_ERROR;
}

AliroError GetPublicKey(CryptoTypes::PublicKey &publicKey)
{
	if (stackfake.reader_public_key_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.reader_public_key_ret);
	}
	publicKey.fill(0);
	publicKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	for (size_t i = 1; i < publicKey.size(); i++) {
		publicKey[i] = static_cast<uint8_t>(0x80 + i);
	}
	return ALIRO_NO_ERROR;
}

bool IsCertificateProvisioned()
{
	return stackfake.certificate_provisioned;
}

AliroError GetIssuerPublicKey(CryptoTypes::PublicKey &publicKey)
{
	publicKey.fill(0);
	publicKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	return ALIRO_NO_ERROR;
}

AliroError GetCertificate(ConstData &certificate)
{
	if (stackfake.certificate_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.certificate_ret);
	}
	certificate.mData = stackfake.certificate;
	certificate.mLength = stackfake.certificate_len;
	return ALIRO_NO_ERROR;
}

} // namespace Reader

/* ---- Ble / Uwb -------------------------------------------------------------- */

namespace Ble
{

size_t GetMaxSessions()
{
	return 2;
}

ProtocolVersion GetProtocolVersion(ConnectionHandle handle)
{
	/* Handle id 9 is the suite's "peer offered something we do not speak". */
	return handle.Id() == 9u ? 0x0200 : 0x0100;
}

} // namespace Ble

namespace Uwb
{

int HandleBleMessage(ConnectionHandle connectionHandle, const uint8_t *data, size_t length)
{
	(void)connectionHandle;
	stackfake.uwb_handle_calls++;
	if (length > sizeof(stackfake.last_uwb_message)) {
		length = sizeof(stackfake.last_uwb_message);
	}
	if (data != nullptr && length > 0U) {
		std::memcpy(stackfake.last_uwb_message, data, length);
	}
	stackfake.last_uwb_message_len = length;
	return stackfake.uwb_handle_ret;
}

} // namespace Uwb

/* ---- Session ---------------------------------------------------------------- */

namespace Session
{

AliroError Send(ConnectionHandle handle, Data data)
{
	if (knob_fires(&stackfake.send_fail_in)) {
		return ALIRO_ERROR_INTERNAL;
	}
	if (stackfake.send_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.send_ret);
	}
	if (stackfake.send_count < STACKFAKE_MAX_SENDS) {
		struct stackfake_send *slot = &stackfake.sends[stackfake.send_count++];
		size_t len = data.mLength;

		if (len > STACKFAKE_SEND_BYTES) {
			len = STACKFAKE_SEND_BYTES;
		}
		if (data.mData != nullptr && len > 0U) {
			std::memcpy(slot->bytes, data.mData, len);
		}
		slot->len = len;
		slot->is_ble = handle.IsBle();
		slot->handle_id = handle.Id();
	}
	return ALIRO_NO_ERROR;
}

void HandleTermination(ConnectionHandle handle)
{
	(void)handle;
	stackfake.termination_calls++;
}

AliroError StartRangingSession(ConnectionHandle handle, uint32_t rangingSessionId,
			       const CryptoTypes::Ursk &ursk, ProtocolVersion protocolVersion)
{
	(void)handle;
	stackfake.ranging_starts++;
	stackfake.last_ranging_session_id = rangingSessionId;
	stackfake.last_ranging_protocol_version = protocolVersion;
	std::memcpy(stackfake.last_ursk, ursk.data(), sizeof(stackfake.last_ursk));
	return static_cast<AliroErrorCode>(stackfake.ranging_ret);
}

} // namespace Session

/* ---- certificate and validity ----------------------------------------------- */

namespace CredentialIssuerCertificate
{

AliroError Validate(const ConstData &certificate, CryptoTypes::PublicKey &publicKey,
		    std::optional<CertificateTimestamps> &timestamps)
{
	(void)certificate;
	if (stackfake.certificate_validate_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.certificate_validate_ret);
	}
	publicKey.fill(0);
	publicKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	publicKey[1] = 0x11; /* same issuer key GetCredentialIssuerPublicKey serves */
	if (stackfake.certificate_has_times) {
		timestamps = CertificateTimestamps{
			Time(stackfake.certificate_valid_from_year, 1, 1, 0, 0, 0),
			Time(stackfake.certificate_valid_until_year, 12, 31, 23, 59, 59)};
	} else {
		timestamps = std::nullopt;
	}
	return ALIRO_NO_ERROR;
}

} // namespace CredentialIssuerCertificate

namespace AccessDocument
{

std::optional<bool> VerifyValidityPeriod(const Time &validFrom, const Time &validUntil)
{
	(void)validFrom;
	(void)validUntil;
	if (!stackfake.validity_known) {
		return std::nullopt;
	}
	return stackfake.validity_answer;
}

} // namespace AccessDocument

/* ---- Os ---------------------------------------------------------------------- */

namespace Os
{

AliroError QueueEvent(void *event)
{
	stackfake.queue_event_calls++;
	stackfake.last_event = event;
	return static_cast<AliroErrorCode>(stackfake.queue_event_ret);
}

namespace Mutex
{

void Lock()
{
	stackfake.mutex_lock_depth++;
	if (stackfake.mutex_lock_depth > stackfake.mutex_max_depth) {
		stackfake.mutex_max_depth = stackfake.mutex_lock_depth;
	}
}

void Unlock()
{
	if (stackfake.mutex_lock_depth > 0U) {
		stackfake.mutex_lock_depth--;
	}
}

} // namespace Mutex

namespace Timer
{

Handle Acquire(Callback callback, void *context)
{
	stackfake.timer_acquire_calls++;
	if (stackfake.timers_exhausted) {
		return kInvalidHandle;
	}
	for (int i = 0; i < STACKFAKE_MAX_TIMERS; i++) {
		if (!stackfake.timers[i].acquired) {
			stackfake.timers[i].acquired = true;
			stackfake.timers[i].running = false;
			stackfake.timers[i].callback = callback;
			stackfake.timers[i].context = context;
			return i;
		}
	}
	return kInvalidHandle;
}

void Release(Handle handle)
{
	stackfake.timer_release_calls++;
	if (handle >= 0 && handle < STACKFAKE_MAX_TIMERS) {
		stackfake.timers[handle] = {};
	}
}

void Start(Handle handle, uint32_t timeoutMs)
{
	stackfake.timer_start_calls++;
	if (handle >= 0 && handle < STACKFAKE_MAX_TIMERS) {
		stackfake.timers[handle].running = true;
		stackfake.timers[handle].timeout_ms = timeoutMs;
	}
}

void Stop(Handle handle)
{
	stackfake.timer_stop_calls++;
	if (handle >= 0 && handle < STACKFAKE_MAX_TIMERS) {
		stackfake.timers[handle].running = false;
	}
}

bool IsRunning(Handle handle)
{
	if (handle < 0 || handle >= STACKFAKE_MAX_TIMERS) {
		return false;
	}
	return stackfake.timers[handle].running;
}

} // namespace Timer

} // namespace Os

/* ---- Crypto ------------------------------------------------------------------ */

namespace Crypto
{

AliroError GenerateRandom(uint8_t *buffer, size_t bufferLength)
{
	if (stackfake.random_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.random_ret);
	}
	/* Deterministic, so the ranging session id derived from the last four
	 * transaction-identifier bytes is a number a suite can predict. */
	for (size_t i = 0; i < bufferLength; i++) {
		buffer[i] = static_cast<uint8_t>(0xa0 + i);
	}
	return ALIRO_NO_ERROR;
}

AliroError GenerateEphemeralKeyPair(CryptoTypes::KeyId &keyId,
				    CryptoTypes::PublicKey &ephemeralPubKey)
{
	stackfake.ephemeral_calls++;
	if (stackfake.ephemeral_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.ephemeral_ret);
	}
	keyId = stackfake.next_key_id++;
	ephemeralPubKey.fill(0);
	ephemeralPubKey[0] = CryptoTypes::kEccP256PublicKeyPrefix;
	for (size_t i = 1; i < ephemeralPubKey.size(); i++) {
		ephemeralPubKey[i] = static_cast<uint8_t>(0x20 + i);
	}
	return ALIRO_NO_ERROR;
}

AliroError ImportSharedKey(const uint8_t *key, size_t keyLength, CryptoTypes::KeyId &keyId)
{
	(void)key;
	(void)keyLength;
	stackfake.import_shared_calls++;
	if (knob_fires(&stackfake.import_shared_fail_in) || stackfake.import_shared_ret != 0) {
		return stackfake.import_shared_ret != 0
			       ? static_cast<AliroErrorCode>(stackfake.import_shared_ret)
			       : ALIRO_ERROR_INTERNAL;
	}
	keyId = stackfake.next_key_id++;
	return ALIRO_NO_ERROR;
}

AliroError ImportSymmetricKey(const uint8_t *key, size_t keyLength, CryptoTypes::KeyId &keyId)
{
	(void)key;
	(void)keyLength;
	stackfake.import_symmetric_calls++;
	if (knob_fires(&stackfake.import_symmetric_fail_in) || stackfake.import_symmetric_ret != 0) {
		return stackfake.import_symmetric_ret != 0
			       ? static_cast<AliroErrorCode>(stackfake.import_symmetric_ret)
			       : ALIRO_ERROR_INTERNAL;
	}
	keyId = stackfake.next_key_id++;
	return ALIRO_NO_ERROR;
}

AliroError DestroyKey(CryptoTypes::KeyId &keyId)
{
	(void)keyId;
	stackfake.destroy_calls++;
	return static_cast<AliroErrorCode>(stackfake.destroy_ret);
}

AliroError GenerateSignature(const uint8_t *msg, const size_t msgLength,
			     CryptoTypes::Signature &signature)
{
	(void)msg;
	(void)msgLength;
	stackfake.sign_calls++;
	if (stackfake.sign_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.sign_ret);
	}
	for (size_t i = 0; i < signature.size(); i++) {
		signature[i] = static_cast<uint8_t>(0x60 + i);
	}
	return ALIRO_NO_ERROR;
}

AliroError VerifySignature(const CryptoTypes::PublicKey &publicKey, const uint8_t *msg,
			   const size_t msgLength, const CryptoTypes::Signature &signature)
{
	(void)publicKey;
	(void)msg;
	(void)msgLength;
	(void)signature;
	stackfake.verify_calls++;
	return static_cast<AliroErrorCode>(stackfake.verify_ret);
}

AliroError RawKeyAgreement(CryptoTypes::KeyId keyId, const CryptoTypes::PublicKey &peerPublicKey,
			   CryptoTypes::SharedSecret &sharedSecret)
{
	(void)keyId;
	(void)peerPublicKey;
	stackfake.key_agreement_calls++;
	if (stackfake.key_agreement_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.key_agreement_ret);
	}
	for (size_t i = 0; i < sharedSecret.size(); i++) {
		sharedSecret[i] = static_cast<uint8_t>(0x30 + i);
	}
	return ALIRO_NO_ERROR;
}

AliroError DeriveSharedKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			   const uint8_t *salt, size_t saltLength, CryptoTypes::KeyId &derivedKeyId)
{
	(void)keyId;
	(void)info;
	(void)infoLength;
	(void)salt;
	stackfake.derive_shared_calls++;
	stackfake.last_salt_len = saltLength;
	if (stackfake.derive_shared_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.derive_shared_ret);
	}
	derivedKeyId = stackfake.next_key_id++;
	return ALIRO_NO_ERROR;
}

AliroError DeriveSymmetricKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			      const uint8_t *salt, size_t saltLength,
			      CryptoTypes::KeyId &derivedKeyId)
{
	(void)keyId;
	(void)salt;
	(void)saltLength;
	stackfake.derive_symmetric_calls++;
	stackfake.last_info_len = infoLength > sizeof(stackfake.last_info)
					  ? sizeof(stackfake.last_info)
					  : infoLength;
	if (info != nullptr && stackfake.last_info_len > 0U) {
		std::memcpy(stackfake.last_info, info, stackfake.last_info_len);
	}
	if (knob_fires(&stackfake.derive_symmetric_fail_in) || stackfake.derive_symmetric_ret != 0) {
		return stackfake.derive_symmetric_ret != 0
			       ? static_cast<AliroErrorCode>(stackfake.derive_symmetric_ret)
			       : ALIRO_ERROR_INTERNAL;
	}
	derivedKeyId = stackfake.next_key_id++;
	return ALIRO_NO_ERROR;
}

AliroError DeriveRawKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			const uint8_t *salt, size_t saltLength, uint8_t *derivedKey,
			size_t derivedKeyLength)
{
	(void)keyId;
	(void)info;
	(void)infoLength;
	stackfake.derive_raw_calls++;
	stackfake.last_salt_len = saltLength > sizeof(stackfake.last_salt)
					  ? sizeof(stackfake.last_salt)
					  : saltLength;
	if (salt != nullptr && stackfake.last_salt_len > 0U) {
		std::memcpy(stackfake.last_salt, salt, stackfake.last_salt_len);
	}
	if (stackfake.derive_raw_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.derive_raw_ret);
	}
	/* A counter pattern, so a suite can see which 32-byte slice of the
	 * 160-byte block became which key. */
	for (size_t i = 0; i < derivedKeyLength; i++) {
		derivedKey[i] = static_cast<uint8_t>(i);
	}
	return ALIRO_NO_ERROR;
}

AliroError AeadEncrypt(CryptoTypes::KeyId keyId, const uint8_t *plainTxt, size_t plainTxtLength,
		       const uint8_t *additionalData, size_t additionalDataLength,
		       const CryptoTypes::Nonce &nonce, uint8_t *cipherText,
		       CryptoTypes::AuthenticationTag &authTag)
{
	(void)additionalData;
	(void)additionalDataLength;
	stackfake.aead_encrypt_calls++;
	stackfake.last_aead_encrypt_key = keyId;
	std::memcpy(stackfake.last_nonce, nonce.data(), nonce.size());
	if (stackfake.aead_encrypt_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.aead_encrypt_ret);
	}
	/* "Encryption" is a copy: the suites read the plaintext straight out of
	 * the recorded frame, which is the whole point of a transport recorder. */
	if (plainTxt != nullptr && cipherText != nullptr && plainTxtLength > 0U) {
		std::memmove(cipherText, plainTxt, plainTxtLength);
	}
	stackfake_tag(keyId, nonce.data(), authTag.data());
	return ALIRO_NO_ERROR;
}

AliroError AeadDecrypt(CryptoTypes::KeyId keyId, const uint8_t *cipherTextWithTag,
		       size_t cipherTextWithTagLength, const uint8_t *additionalData,
		       size_t additionalDataLength, const CryptoTypes::Nonce &nonce,
		       uint8_t *plainText, size_t &plainTextLength)
{
	uint8_t expected[Aliro::CryptoTypes::kAuthenticationTagLength];
	size_t bodyLength;

	(void)additionalData;
	(void)additionalDataLength;
	stackfake.aead_decrypt_calls++;
	stackfake.last_aead_decrypt_key = keyId;
	std::memcpy(stackfake.last_nonce, nonce.data(), nonce.size());
	if (stackfake.aead_decrypt_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.aead_decrypt_ret);
	}
	if (cipherTextWithTagLength < CryptoTypes::kAuthenticationTagLength) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	bodyLength = cipherTextWithTagLength - CryptoTypes::kAuthenticationTagLength;

	/* The tag must be the one this fake would have produced for this key
	 * and nonce. That is what makes a wrong key or a skipped counter fail
	 * here the way it would on the device -- and it is arithmetic, not a
	 * MAC, so it is no evidence about the real AEAD. */
	stackfake_tag(keyId, nonce.data(), expected);
	if (!stackfake.aead_decrypt_ignore_tag &&
	    std::memcmp(cipherTextWithTag + bodyLength, expected, sizeof(expected)) != 0) {
		return ALIRO_INVALID_AUTHENTICATION_TAG;
	}
	if (bodyLength > plainTextLength) {
		return ALIRO_NO_MEMORY;
	}
	if (plainText != nullptr && bodyLength > 0U) {
		std::memmove(plainText, cipherTextWithTag, bodyLength);
	}
	plainTextLength = bodyLength;
	return ALIRO_NO_ERROR;
}

AliroError Encrypt(const uint8_t *plainText, size_t plainTextLength, uint8_t *cipherText)
{
	stackfake.encrypt_calls++;
	if (stackfake.encrypt_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.encrypt_ret);
	}
	for (size_t i = 0; i < plainTextLength; i++) {
		cipherText[i] = static_cast<uint8_t>(plainText[i] ^ 0x5au);
	}
	return ALIRO_NO_ERROR;
}

AliroError Sha256(const uint8_t *data, size_t dataLength, CryptoTypes::Sha256Hash &hash)
{
	stackfake.sha256_calls++;
	if (stackfake.sha256_ret != 0) {
		return static_cast<AliroErrorCode>(stackfake.sha256_ret);
	}
	if (stackfake.sha256_forced) {
		std::memcpy(hash.data(), stackfake.sha256_force, hash.size());
		return ALIRO_NO_ERROR;
	}
	/* Length-and-content sensitive enough that two different inputs almost
	 * never collide, which is all the access-document digest check needs.
	 * It is not SHA-256 and nothing here depends on it being one. */
	hash.fill(0);
	for (size_t i = 0; i < dataLength; i++) {
		hash[i % hash.size()] = static_cast<uint8_t>(hash[i % hash.size()] * 31u + data[i] +
							     static_cast<uint8_t>(i));
	}
	hash[0] = static_cast<uint8_t>(hash[0] ^ dataLength);
	return ALIRO_NO_ERROR;
}

} // namespace Crypto

} // namespace Aliro::Interface
