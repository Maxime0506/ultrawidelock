/**
 * @file session.cpp
 * credential reader BLE session state machine and cryptographic session context. Manages NFC APDU
 * limits, response timeouts, connection setup, fast-path and standard key derivation, message
 * encryption and decryption, and reader-status notifications. Processes events from the BLE
 * transport and application layer.
 */
#include "protocol/nfc_auth.h"
#include "protocol/ble_message.h"
#include "protocol/ble_timeout.h"
#include "protocol/nfc_select.h"

#include "ultrawidelock_stepup.h"

#include <aliro/aliro.h>
#include <aliro/interface.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>

LOG_MODULE_DECLARE(ultrawidelock_cred_source, CONFIG_NCS_ALIRO_LOG_LEVEL_VALUE);

namespace Aliro
{
namespace
{

constexpr size_t kNfcSessions = 1;
constexpr size_t kBleSessions = CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS;
constexpr size_t kSessionCount = kNfcSessions + kBleSessions;
constexpr size_t kApduBufferSize = CONFIG_ULTRAWIDELOCK_CRED_APDU_BUFFER_SIZE;
constexpr size_t kBleFrameCapacity =
	kApduBufferSize + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE;
constexpr size_t kBleMaxCommandData = 255;
constexpr size_t kBleMaxResponseData = 256;
/* Match the reference NFC reader's short-APDU policy. Peer support for
 * extended APDUs is only a capability; it does not require using them. NFC
 * transports may subdivide these stack-level commands further if needed. */
constexpr size_t kNfcMaxCommandData = 255;
constexpr size_t kNfcMaxResponseData = 256;
constexpr size_t kProprietaryInformationSize = 260;
constexpr uint8_t kAuthenticationPolicy = 0x01; /* User Device policy. */
constexpr uint8_t kStandardCommandParameters = 0x00;
constexpr uint8_t kAuth1PublicKeyRequested = 0x01;
constexpr uint32_t kReaderAuthenticationUsage = 0x415d9569;
constexpr uint32_t kDeviceAuthenticationUsage = 0x4e887b4c;
constexpr uint32_t kInitialCounter = 1;
constexpr uint32_t kResponseTimeoutMs = 1500;
constexpr ProtocolVersion kBleProtocolVersions[]{0x0100};
constexpr size_t kMaxKpersistentKeys =
	CONFIG_MAX_NUMBER_OF_KPERSISTENT + CONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS;

enum class SessionState : uint8_t {
	Free,
	SelectingExpedited,
	AwaitingAuth0,
	AwaitingAuth1,
	SelectingStepUp,
	SendingStepUpEnvelope,
	AwaitingStepUpResponse,
	AwaitingExchangeResponse,
	AccessComplete,
	BleConnected,
	UwbRanging,
};

/**
 * Session context holding all state needed to manage an credential session: connection handle,
 * protocol version, cryptographic keys, buffers, counters, and access document details.
 */
struct SessionContext {
	std::optional<ConnectionHandle> mHandle;
	SessionState mState{SessionState::Free};
	ProtocolVersion mProtocolVersion{};
	Identifier mReaderIdentifier{};
	CryptoTypes::PublicKey mReaderPublicKey{};
	CryptoTypes::PublicKey mReaderEphemeralPublicKey{};
	CryptoTypes::PublicKey mCredentialEphemeralPublicKey{};
	CryptoTypes::TransactionIdentifier mTransactionIdentifier{};
	std::array<uint8_t, kProprietaryInformationSize> mProprietaryInformation{};
	size_t mProprietaryInformationLength{};
	uint8_t mCommandParameters{kStandardCommandParameters};
	uint8_t mAuthenticationPolicy{kAuthenticationPolicy};
	uint32_t mDeviceCounter{kInitialCounter};
	uint32_t mReaderCounter{kInitialCounter};
	CryptoTypes::KeyId mReaderEphemeralKeyId{};
	CryptoTypes::KeyId mKdhKeyId{};
	CryptoTypes::KeyId mExpeditedReaderKeyId{};
	CryptoTypes::KeyId mExpeditedDeviceKeyId{};
	CryptoTypes::KeyId mStepUpKeyId{};
	CryptoTypes::KeyId mStepUpReaderKeyId{};
	CryptoTypes::KeyId mStepUpDeviceKeyId{};
	CryptoTypes::KeyId mBleKeyId{};
	CryptoTypes::KeyId mBleReaderKeyId{};
	CryptoTypes::KeyId mBleDeviceKeyId{};
	CryptoTypes::KeyId mKpersistentKeyId{};
	std::array<CryptoTypes::KeyId, kMaxKpersistentKeys> mKpersistentKeyIds{};
	size_t mKpersistentKeyCount{};
	CryptoTypes::Ursk mUrsk{};
	std::array<uint8_t, kApduBufferSize> mTxBuffer{};
	std::array<uint8_t, kApduBufferSize> mPlaintextBuffer{};
	std::array<uint8_t, kApduBufferSize> mSessionDataBuffer{};
	std::array<uint8_t, kApduBufferSize> mExchangeBuffer{};
	std::array<uint8_t,
		   kApduBufferSize + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE>
		mBleBuffer{};
	std::array<uint8_t, kBleFrameCapacity> mBleRxBuffer{};
	size_t mBleRxLength{};
	size_t mExchangeLength{};
	size_t mExchangeOffset{};
	size_t mResponseLength{};
	size_t mMaxCommandData{255};
	size_t mMaxResponseData{256};
	bool mUseExtendedApdus{};
	bool mLastEnvelope{};
	CryptoTypes::PublicKey mCredentialPublicKey{};
	std::array<uint8_t, 128> mRequestedElement{};
	size_t mRequestedElementLength{};
	bool mIntentToStore{};
	bool mFastAccess{};
	bool mAccessProcessed{};
	bool mExchangeUsesStepUpKeys{};
	CryptoTypes::KeyId mMatchedKpersistentKeyId{};
	uint32_t mBleDeviceCounter{kInitialCounter};
	uint32_t mBleReaderCounter{kInitialCounter};
	Interface::Os::Timer::Handle mResponseTimer{Interface::Os::Timer::kInvalidHandle};
	struct ultrawidelock_cred_ble_timeout_state mResponseTimeout{};
	uint32_t mResponseTimerGeneration{};
};

/**
 * Sets the session's maximum command and response data lengths from the NFC select response,
 * constrained by the session's buffer sizes and NFC protocol limits, and records whether
 * extended-length APDUs are supported.
 */
void ApplyNfcApduLimits(SessionContext &session,
			const struct ultrawidelock_cred_select_response &selected)
{
	session.mMaxCommandData = std::min({selected.max_command_data_length,
					    session.mTxBuffer.size() - 9, kNfcMaxCommandData});
	session.mMaxResponseData =
		std::min({selected.max_response_data_length, session.mExchangeBuffer.size() - 2,
			  kNfcMaxResponseData});
	session.mUseExtendedApdus = selected.extended_length_supported != 0;
}

std::array<SessionContext, kSessionCount> sSessions{};

/**
 * Atomic generation counter used to invalidate stale response timeout expirations.
 */
struct ResponseTimerContext {
	std::atomic<uint32_t> mGeneration{0};
};

std::array<ResponseTimerContext, kSessionCount> sResponseTimerContexts{};

constexpr uint32_t kSessionDataEventMagic = 0x41525844;     /* "ARXD" */
constexpr uint32_t kResponseTimeoutEventMagic = 0x4152544f; /* "ARTO" */
constexpr size_t kSessionDataEventCapacity =
	kApduBufferSize + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE;

/**
 * Internal event structure for session data notifications; carries the connection handle and a copy
 * of the received data.
 */
struct SessionDataEvent {
	/* k_fifo reserves and updates the first pointer-sized word. */
	void *mFifoReserved{};
	uint32_t mMagic{kSessionDataEventMagic};
	ConnectionHandle mHandle;
	size_t mLength{};
	std::array<uint8_t, kSessionDataEventCapacity> mData{};

	/**
	 * Initializes a session data event by copying the connection handle and data payload.
	 */
	SessionDataEvent(ConnectionHandle handle, Data data)
		: mHandle(handle), mLength(data.mLength)
	{
		std::copy_n(data.mData, data.mLength, mData.begin());
	}
};

static_assert(offsetof(SessionDataEvent, mFifoReserved) == 0);

/**
 * Internal event structure for response timeout notifications; carries session index and generation
 * number to detect stale timeouts.
 */
struct ResponseTimeoutEvent {
	void *mFifoReserved{};
	uint32_t mMagic{kResponseTimeoutEventMagic};
	size_t mSessionIndex{};
	uint32_t mGeneration{};

	/**
	 * Initializes a response timeout event with the given session index and generation number.
	 */
	ResponseTimeoutEvent(size_t sessionIndex, uint32_t generation)
		: mSessionIndex(sessionIndex), mGeneration(generation)
	{
	}
};

static_assert(offsetof(ResponseTimeoutEvent, mFifoReserved) == 0);

/**
 * Internal event structure reserved by the FIFO queue mechanism; carries a magic number for
 * validation.
 */
struct EventHeader {
	void *mFifoReserved;
	uint32_t mMagic;
};

/**
 * RAII lock guard that acquires the stack mutex on construction and releases it on destruction.
 */
class StackLock
{
      public:
	StackLock()
	{
		Interface::Os::Mutex::Lock();
	}
	/**
	 * Releases the stack mutex.
	 */
	~StackLock()
	{
		Interface::Os::Mutex::Unlock();
	}
	StackLock(const StackLock &) = delete;
	/**
	 * Deleted copy-assignment operator to prevent copying of the lock guard.
	 */
	StackLock &operator=(const StackLock &) = delete;
};

/**
 * Returns a pointer to the session matching the given connection handle, or nullptr if no session
 * is found.
 */
SessionContext *FindSession(ConnectionHandle handle)
{
	for (auto &session : sSessions) {
		if (session.mHandle.has_value() && session.mHandle.value() == handle) {
			return &session;
		}
	}
	return nullptr;
}

/**
 * Returns the index of the given session in the global session array.
 */
size_t SessionIndex(const SessionContext &session)
{
	return static_cast<size_t>(&session - sSessions.data());
}

/**
 * Atomically increments the generation counter for the session's response timer, updates the
 * session's stored generation, and returns the new value.
 */
uint32_t NextResponseTimerGeneration(SessionContext &session)
{
	auto &generation = sResponseTimerContexts[SessionIndex(session)].mGeneration;
	const uint32_t next = generation.fetch_add(1, std::memory_order_relaxed) + 1;
	session.mResponseTimerGeneration = next;
	return next;
}

/**
 * Response timeout callback invoked by the timer; validates the context pointer, constructs a
 * ResponseTimeoutEvent, and queues it for deferred processing; logs errors if event allocation or
 * queueing fails.
 */
void ResponseTimerExpired(void *context)
{
	auto *timerContext = static_cast<ResponseTimerContext *>(context);
	if (timerContext == nullptr || timerContext < sResponseTimerContexts.data() ||
	    timerContext >= sResponseTimerContexts.data() + sResponseTimerContexts.size()) {
		return;
	}
	const size_t sessionIndex =
		static_cast<size_t>(timerContext - sResponseTimerContexts.data());
	const uint32_t generation = timerContext->mGeneration.load(std::memory_order_relaxed);
	auto *event = new (std::nothrow) ResponseTimeoutEvent(sessionIndex, generation);
	if (event == nullptr) {
		LOG_ERR("Cannot defer credential response timeout: no memory");
		return;
	}
	const AliroError status = Interface::Os::QueueEvent(event);
	if (status != ALIRO_NO_ERROR) {
		LOG_ERR("Cannot defer credential response timeout: %s", status.ToString());
		delete event;
	}
}

/**
 * Allocates a free session slot, initializes it with the connection handle, and acquires a response
 * timer for BLE sessions; returns nullptr if no slots are available or timer acquisition fails.
 */
SessionContext *AllocateSession(ConnectionHandle handle)
{
	for (auto &session : sSessions) {
		if (!session.mHandle.has_value()) {
			session = {};
			session.mHandle = handle;
			if (handle.IsBle()) {
				NextResponseTimerGeneration(session);
				session.mResponseTimer = Interface::Os::Timer::Acquire(
					ResponseTimerExpired,
					&sResponseTimerContexts[SessionIndex(session)]);
				if (session.mResponseTimer ==
				    Interface::Os::Timer::kInvalidHandle) {
					session = {};
					return nullptr;
				}
			}
			return &session;
		}
	}
	return nullptr;
}

/**
 * Destroys a transient cryptographic key if it is non-zero, logs a warning on failure, and zeros
 * the key ID.
 */
void DestroyKey(CryptoTypes::KeyId &keyId)
{
	if (keyId != 0) {
		const AliroError error = Interface::Crypto::DestroyKey(keyId);
		if (error != ALIRO_NO_ERROR) {
			LOG_WRN("Failed to destroy transient credential key: %d", error.ToInt());
		}
		keyId = 0;
	}
}

/**
 * Releases the response timer, destroys all session keys, zeros the URSK, and clears the session
 * context.
 */
void ResetSession(SessionContext &session)
{
	if (session.mResponseTimer != Interface::Os::Timer::kInvalidHandle) {
		NextResponseTimerGeneration(session);
		Interface::Os::Timer::Release(session.mResponseTimer);
		session.mResponseTimer = Interface::Os::Timer::kInvalidHandle;
	}
	DestroyKey(session.mReaderEphemeralKeyId);
	DestroyKey(session.mKdhKeyId);
	DestroyKey(session.mExpeditedReaderKeyId);
	DestroyKey(session.mExpeditedDeviceKeyId);
	DestroyKey(session.mStepUpKeyId);
	DestroyKey(session.mStepUpReaderKeyId);
	DestroyKey(session.mStepUpDeviceKeyId);
	DestroyKey(session.mBleKeyId);
	DestroyKey(session.mBleReaderKeyId);
	DestroyKey(session.mBleDeviceKeyId);
	DestroyKey(session.mKpersistentKeyId);
	std::fill(session.mUrsk.begin(), session.mUrsk.end(), 0);
	session = {};
}

/**
 * Observes an incoming or outgoing BLE message to update response timeout state (arm, stop, or
 * terminate the timer); returns true if the timeout action indicates termination.
 */
bool ObserveResponseTimeoutMessage(SessionContext &session,
				   enum ultrawidelock_cred_ble_timeout_direction direction,
				   const uint8_t *data, size_t length)
{
	if (session.mResponseTimer == Interface::Os::Timer::kInvalidHandle) {
		return false;
	}
	enum ultrawidelock_cred_ble_timeout_message message;
	if (ultrawidelock_cred_ble_timeout_classify(data, length, &message) != ULTRAWIDELOCK_CRED_BLE_OK) {
		return false;
	}
	const enum ultrawidelock_cred_ble_timeout_action action =
		ultrawidelock_cred_ble_timeout_observe(&session.mResponseTimeout, direction, message);
	switch (action) {
	case ULTRAWIDELOCK_CRED_BLE_TIMEOUT_ARM:
		NextResponseTimerGeneration(session);
		Interface::Os::Timer::Start(session.mResponseTimer, kResponseTimeoutMs);
		break;
	case ULTRAWIDELOCK_CRED_BLE_TIMEOUT_STOP:
	case ULTRAWIDELOCK_CRED_BLE_TIMEOUT_TERMINATE:
		NextResponseTimerGeneration(session);
		Interface::Os::Timer::Stop(session.mResponseTimer);
		break;
	case ULTRAWIDELOCK_CRED_BLE_TIMEOUT_NO_ACTION:
	default:
		break;
	}
	return action == ULTRAWIDELOCK_CRED_BLE_TIMEOUT_TERMINATE;
}

/**
 * Appends data to a buffer, advancing the offset by the data length, and returns true if the append
 * succeeded without overflow; returns false if data is null, offset exceeds capacity, or the data
 * length exceeds remaining capacity.
 */
bool Append(uint8_t *buffer, size_t capacity, size_t &offset, const uint8_t *data, size_t length)
{
	if (data == nullptr || offset > capacity || length > capacity - offset) {
		return false;
	}
	std::copy_n(data, length, buffer + offset);
	offset += length;
	return true;
}

/**
 * Appends reader public key, label, reader identifier, interface byte, version TLV, reader
 * ephemeral public key, transaction identifier, flags, and proprietary information to the salt
 * buffer; returns false if any append overflows the buffer.
 */
bool AppendCommonSalt(SessionContext &session, uint8_t *salt, size_t capacity, size_t &offset,
		      const char label[12])
{
	static constexpr uint8_t kVersionTlv[]{0x5c, 0x02, 0x01, 0x00};
	const uint8_t interfaceByte = session.mHandle->IsNfc() ? 0x5e : 0xc3;
	const uint8_t flags[]{session.mCommandParameters, session.mAuthenticationPolicy};
	return Append(salt, capacity, offset, session.mReaderPublicKey.data() + 1, 32) &&
	       Append(salt, capacity, offset, reinterpret_cast<const uint8_t *>(label), 12) &&
	       Append(salt, capacity, offset, session.mReaderIdentifier.data(),
		      session.mReaderIdentifier.size()) &&
	       Append(salt, capacity, offset, &interfaceByte, 1) &&
	       Append(salt, capacity, offset, kVersionTlv, sizeof(kVersionTlv)) &&
	       Append(salt, capacity, offset, session.mReaderEphemeralPublicKey.data() + 1, 32) &&
	       Append(salt, capacity, offset, session.mTransactionIdentifier.data(),
		      session.mTransactionIdentifier.size()) &&
	       Append(salt, capacity, offset, flags, sizeof(flags)) &&
	       Append(salt, capacity, offset, session.mProprietaryInformation.data(),
		      session.mProprietaryInformationLength);
}

/**
 * Performs key agreement on the ephemeral keys, derives the Kdh key via X9.63 KDF, derives 160
 * bytes of keying material from Kdh, imports the expedited reader and device keys, imports the
 * StepUpSK and BleSK roots, and returns early on error.
 */
AliroError DeriveVolatileKeys(SessionContext &session)
{
	CryptoTypes::SharedSecret sharedSecret{};
	AliroError error = Interface::Crypto::RawKeyAgreement(
		session.mReaderEphemeralKeyId, session.mCredentialEphemeralPublicKey, sharedSecret);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	std::array<uint8_t, 32 + 4 + CryptoTypes::kTransactionIdentifierLength> x963Input{};
	std::copy(sharedSecret.begin(), sharedSecret.end(), x963Input.begin());
	x963Input[35] = 1; /* X9.63 counter, unsigned big-endian. */
	std::copy(session.mTransactionIdentifier.begin(), session.mTransactionIdentifier.end(),
		  x963Input.begin() + 36);
	CryptoTypes::Sha256Hash kdh{};
	error = Interface::Crypto::Sha256(x963Input.data(), x963Input.size(), kdh);
	std::fill(sharedSecret.begin(), sharedSecret.end(), 0);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	error = Interface::Crypto::ImportSharedKey(kdh.data(), kdh.size(), session.mKdhKeyId);
	std::fill(kdh.begin(), kdh.end(), 0);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	std::array<uint8_t, kApduBufferSize> salt{};
	size_t saltLength = 0;
	static constexpr char kVolatileLabel[12] = {'V', 'o', 'l', 'a', 't', 'i',
						    'l', 'e', '*', '*', '*', '*'};
	if (!AppendCommonSalt(session, salt.data(), salt.size(), saltLength, kVolatileLabel)) {
		return ALIRO_NO_MEMORY;
	}
	std::array<uint8_t, 160> derivedKeys{};
	error = Interface::Crypto::DeriveRawKey(
		session.mKdhKeyId, session.mCredentialEphemeralPublicKey.data() + 1, 32,
		salt.data(), saltLength, derivedKeys.data(), derivedKeys.size());
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	error = Interface::Crypto::ImportSymmetricKey(derivedKeys.data(), 32,
						      session.mExpeditedReaderKeyId);
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::ImportSymmetricKey(derivedKeys.data() + 32, 32,
							      session.mExpeditedDeviceKeyId);
	}
	if (error == ALIRO_NO_ERROR) {
		/* StepUpSK is an HKDF root used to derive the directional AES keys. */
		error = Interface::Crypto::ImportSharedKey(derivedKeys.data() + 64, 32,
							   session.mStepUpKeyId);
	}
	if (error == ALIRO_NO_ERROR && session.mHandle->IsBle()) {
		/* BleSK likewise derives BleSKReader and BleSKDevice. */
		error = Interface::Crypto::ImportSharedKey(derivedKeys.data() + 96, 32,
							   session.mBleKeyId);
	}
	std::copy_n(derivedKeys.data() + 128, session.mUrsk.size(), session.mUrsk.begin());
	std::fill(derivedKeys.begin(), derivedKeys.end(), 0);
	return error;
}

/**
 * Derives the persistent key by computing a salt from the credential public key and deriving a
 * shared key using the Kdh key; returns an error if the salt overflows or key derivation fails.
 */
AliroError DerivePersistentKey(SessionContext &session,
			       const CryptoTypes::PublicKey &credentialPublicKey)
{
	std::array<uint8_t, kApduBufferSize> salt{};
	size_t saltLength = 0;
	static constexpr char kPersistentLabel[12] = {'P', 'e', 'r', 's', 'i', 's',
						      't', 'e', 'n', 't', '*', '*'};
	if (!AppendCommonSalt(session, salt.data(), salt.size(), saltLength, kPersistentLabel) ||
	    !Append(salt.data(), salt.size(), saltLength, credentialPublicKey.data() + 1, 32)) {
		return ALIRO_NO_MEMORY;
	}
	return Interface::Crypto::DeriveSharedKey(
		session.mKdhKeyId, session.mCredentialEphemeralPublicKey.data() + 1, 32,
		salt.data(), saltLength, session.mKpersistentKeyId);
}

/**
 * Returns true if the plaintext is exactly 48 bytes and contains the fixed-width cryptogram
 * structure (signaling bitmap followed by two signed timestamps) as specified in Table 8-6; returns
 * false otherwise.
 */
bool IsValidCryptogramPlaintext(const uint8_t *plaintext, size_t length)
{
	/* Table 8-6: signaling bitmap followed by both fixed-width signed
	 * timestamps. Unknown or reordered fields are not permitted here. */
	return plaintext != nullptr && length == 48 && plaintext[0] == 0x5e &&
	       plaintext[1] == 0x02 && plaintext[4] == 0x91 && plaintext[5] == 0x14 &&
	       plaintext[26] == 0x92 && plaintext[27] == 0x14;
}

/**
 * Attempts fast-path access by deriving keys from a stored persistent key, decrypting the
 * cryptogram, validating its plaintext, checking whether a new access document is required, and
 * importing the expedited and BLE keys on success; returns the validation status or ALIRO_NO_ERROR
 * on a successful fast match.
 */
AliroError TryFastKey(SessionContext &session, CryptoTypes::KeyId kpersistentKeyId,
		      const uint8_t *cryptogram, size_t cryptogramLength, bool &matched,
		      bool &requiresStandard)
{
	matched = false;
	requiresStandard = false;
	CryptoTypes::PublicKey credentialPublicKey{};
	AliroError error = Interface::Access::GetAccessCredentialPublicKey(kpersistentKeyId,
									   credentialPublicKey);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_CREDENTIAL_LOOKUP status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	std::array<uint8_t, kApduBufferSize> salt{};
	size_t saltLength = 0;
	static constexpr char kFastLabel[12] = {'V', 'o', 'l', 'a', 't', 'i',
						'l', 'e', 'F', 'a', 's', 't'};
	if (!AppendCommonSalt(session, salt.data(), salt.size(), saltLength, kFastLabel) ||
	    !Append(salt.data(), salt.size(), saltLength, credentialPublicKey.data() + 1, 32)) {
		return ALIRO_NO_MEMORY;
	}
	std::array<uint8_t, 160> derivedKeys{};
	error = Interface::Crypto::DeriveRawKey(
		kpersistentKeyId, session.mCredentialEphemeralPublicKey.data() + 1, 32, salt.data(),
		saltLength, derivedKeys.data(), derivedKeys.size());
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_DERIVE status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	CryptoTypes::KeyId cryptogramKeyId{};
	error = Interface::Crypto::ImportSymmetricKey(derivedKeys.data(), 32, cryptogramKeyId);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_IMPORT status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		std::fill(derivedKeys.begin(), derivedKeys.end(), 0);
		return error;
	}
	std::array<uint8_t, 48> plaintext{};
	size_t plaintextLength = plaintext.size();
	const CryptoTypes::Nonce nonce{};
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_DECRYPT_BEGIN cryptogram_len=%zu", cryptogramLength);
#endif
	error = Interface::Crypto::AeadDecrypt(cryptogramKeyId, cryptogram, cryptogramLength,
					       nullptr, 0, nonce, plaintext.data(),
					       plaintextLength);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_DECRYPT_RESULT status=%d plaintext_len=%zu",
		error.ToInt(), plaintextLength);
#endif
	DestroyKey(cryptogramKeyId);
	if (error != ALIRO_NO_ERROR ||
	    !IsValidCryptogramPlaintext(plaintext.data(), plaintextLength)) {
		std::fill(derivedKeys.begin(), derivedKeys.end(), 0);
		return error == ALIRO_NO_ERROR ? AliroError(ALIRO_INVALID_DATA_FORMAT) : error;
	}
	matched = true;
	std::optional<Timestamp> credentialSignedTimestamp;
	if (plaintext[4] == 0x91 && plaintext[5] == kTimestampLength) {
		credentialSignedTimestamp.emplace();
		std::copy_n(plaintext.data() + 6, credentialSignedTimestamp->size(),
			    credentialSignedTimestamp->begin());
	}
	/* A successfully authenticated fast cryptogram can still require the
	 * standard phase when the device advertises a newer Access Document. */
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_ACCESS_DOCUMENT_CHECK_BEGIN");
#endif
	const auto accessDocumentRequest = Interface::Access::GetAccessDocumentRequestParameters(
		credentialPublicKey, credentialSignedTimestamp);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_ACCESS_DOCUMENT_CHECK_RESULT requested=%u",
		static_cast<unsigned int>(accessDocumentRequest.has_value()));
#endif
	if (accessDocumentRequest.has_value()) {
		requiresStandard = true;
		std::fill(derivedKeys.begin(), derivedKeys.end(), 0);
		return ALIRO_NO_ERROR;
	}

	error = Interface::Crypto::ImportSymmetricKey(derivedKeys.data() + 32, 32,
						      session.mExpeditedReaderKeyId);
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::ImportSymmetricKey(derivedKeys.data() + 64, 32,
							      session.mExpeditedDeviceKeyId);
	}
	if (error == ALIRO_NO_ERROR && session.mHandle->IsBle()) {
		error = Interface::Crypto::ImportSharedKey(derivedKeys.data() + 96, 32,
							   session.mBleKeyId);
	}
	std::copy_n(derivedKeys.data() + 128, session.mUrsk.size(), session.mUrsk.begin());
	std::fill(derivedKeys.begin(), derivedKeys.end(), 0);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	session.mCredentialPublicKey = credentialPublicKey;
	session.mMatchedKpersistentKeyId = kpersistentKeyId;
	session.mFastAccess = true;
	return ALIRO_NO_ERROR;
}

/**
 * For NFC, sends the command directly; for BLE, frames the command in a BLE message, sends it, and
 * observes the timeout message.
 */
AliroError SendApCommand(SessionContext &session, const uint8_t *command, size_t commandLength)
{
	if (!session.mHandle->IsBle()) {
		return Interface::Session::Send(*session.mHandle,
						{const_cast<uint8_t *>(command), commandLength});
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AP_COMMAND_BUILD state=%u ins=0x%02x len=%zu",
		static_cast<unsigned int>(session.mState), commandLength > 1 ? command[1] : 0xff,
		commandLength);
#endif
	size_t messageLength = 0;
	if (ultrawidelock_cred_ble_build_message(ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP, 0, command,
						 commandLength, session.mBleBuffer.data(),
						 session.mBleBuffer.size(),
						 &messageLength) != ULTRAWIDELOCK_CRED_BLE_OK) {
		return ALIRO_NO_MEMORY;
	}
	const AliroError error = Interface::Session::Send(
		*session.mHandle, {session.mBleBuffer.data(), messageLength});
	if (error == ALIRO_NO_ERROR) {
		ObserveResponseTimeoutMessage(session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_OUTGOING,
					      session.mBleBuffer.data(), messageLength);
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AP_COMMAND_SEND state=%u framed_len=%zu status=%d",
		static_cast<unsigned int>(session.mState), messageLength, error.ToInt());
#endif
	return error;
}

AliroError ProcessAccess(SessionContext &session);
AliroError SendUrskExchange(SessionContext &session);
AliroError SendNfcCompletionExchange(SessionContext &session, bool useStepUpKeys);

/**
 * Acquires the reader identity, public key, and ephemeral key pair, generates a random transaction
 * identifier, optionally loads persistent credential key IDs for fast-path attempts, builds and
 * sends the Auth0 command, and transitions to AwaitingAuth0.
 */
AliroError SendAuth0(SessionContext &session)
{
	AliroError error = Interface::Reader::GetIdentifier(session.mReaderIdentifier);
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Reader::GetPublicKey(session.mReaderPublicKey);
	}
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::GenerateEphemeralKeyPair(
			session.mReaderEphemeralKeyId, session.mReaderEphemeralPublicKey);
	}
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::GenerateRandom(session.mTransactionIdentifier.data(),
							  session.mTransactionIdentifier.size());
	}
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	/* Expedited-fast is attempted only when the application has persistent
	 * credentials to try. A failed cryptogram match falls back to standard. */
	size_t count = 0;
	if (Interface::Access::GetKpersistentCount(count) == ALIRO_NO_ERROR && count != 0) {
		count = std::min(count, session.mKpersistentKeyIds.size());
		session.mKpersistentKeyCount = count;
		if (Interface::Access::GetKpersistentKeyIds(session.mKpersistentKeyIds.data(),
							    session.mKpersistentKeyCount) ==
			    ALIRO_NO_ERROR &&
		    session.mKpersistentKeyCount != 0) {
			session.mCommandParameters = 0x01;
		} else {
			session.mKpersistentKeyCount = 0;
		}
	}

	const ultrawidelock_cred_auth0_command params{
		.command_parameters = session.mCommandParameters,
		.authentication_policy = session.mAuthenticationPolicy,
		.protocol_version = session.mProtocolVersion,
		.reader_ephemeral_public_key = session.mReaderEphemeralPublicKey.data(),
		.transaction_identifier = session.mTransactionIdentifier.data(),
		.reader_identifier = session.mReaderIdentifier.data(),
	};
	size_t length = 0;
	if (ultrawidelock_cred_build_auth0_command(&params, session.mTxBuffer.data(),
					  session.mTxBuffer.size(), &length) != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	session.mState = SessionState::AwaitingAuth0;
	return SendApCommand(session, session.mTxBuffer.data(), length);
}

/**
 * Decrypt and validate Auth0 response; attempt fast-key authentication if enabled, otherwise derive
 * volatile keys and build Auth1 command.
 *
 * Fails on parse, fast-key, key derivation, signature generation, or command build errors. Attempts
 * each persistent fast key in order if fast-path enabled; succeeds early if matched and does not
 * require standard phase. Destroys expedited and volatile keys if fast-path active. Sets state to
 * AwaitingAuth1. On trace enabled, logs each fast-key trial and final derivation status.
 */
AliroError HandleAuth0Response(SessionContext &session, Data data)
{
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH0_RESPONSE_BEGIN len=%zu fast=%u keys=%zu", data.mLength,
		static_cast<unsigned int>((session.mCommandParameters & 1) != 0),
		session.mKpersistentKeyCount);
#endif
	ultrawidelock_cred_auth0_response response;
	const int parseStatus = ultrawidelock_cred_parse_auth0_response(
		data.mData, data.mLength, session.mCommandParameters & 1, &response);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH0_PARSE status=%d cryptogram_len=%zu", parseStatus,
		response.cryptogram_length);
#endif
	if (parseStatus != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	std::copy_n(response.credential_ephemeral_public_key,
		    session.mCredentialEphemeralPublicKey.size(),
		    session.mCredentialEphemeralPublicKey.begin());
	if ((session.mCommandParameters & 1) != 0) {
		for (size_t i = 0; i < session.mKpersistentKeyCount; ++i) {
			bool matched = false;
			bool requiresStandard = false;
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
			LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_KEY_BEGIN index=%zu", i);
#endif
			const AliroError fastStatus = TryFastKey(
				session, session.mKpersistentKeyIds[i], response.cryptogram,
				response.cryptogram_length, matched, requiresStandard);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
			LOG_INF("ULTRAWIDELOCK_TRACE SOURCE FAST_KEY_RESULT index=%zu status=%d matched=%u "
				"standard=%u",
				i, fastStatus.ToInt(), static_cast<unsigned int>(matched),
				static_cast<unsigned int>(requiresStandard));
#endif
			if (matched) {
				if (fastStatus != ALIRO_NO_ERROR) {
					return fastStatus;
				}
				if (!requiresStandard) {
					if (session.mHandle->IsBle()) {
						return SendUrskExchange(session);
					}
					const AliroError accessStatus = ProcessAccess(session);
					if (accessStatus != ALIRO_NO_ERROR) {
						return accessStatus;
					}
					return SendNfcCompletionExchange(session, false);
				}
				break;
			}
			/* Authentication-tag failure is an expected trial result. Other
			 * per-key errors are also isolated so one stale record cannot prevent
			 * the standards-required fallback to expedited-standard. */
		}
		DestroyKey(session.mExpeditedReaderKeyId);
		DestroyKey(session.mExpeditedDeviceKeyId);
		DestroyKey(session.mBleKeyId);
		std::fill(session.mUrsk.begin(), session.mUrsk.end(), 0);
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE VOLATILE_KEYS_BEGIN");
#endif
	AliroError error = DeriveVolatileKeys(session);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE VOLATILE_KEYS_RESULT status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	std::array<uint8_t, ULTRAWIDELOCK_CRED_AUTH_DATA_SIZE> authenticationData{};
	if (ultrawidelock_cred_build_authentication_data(
		    session.mReaderIdentifier.data(), session.mCredentialEphemeralPublicKey.data(),
		    session.mReaderEphemeralPublicKey.data(), session.mTransactionIdentifier.data(),
		    kReaderAuthenticationUsage, authenticationData.data()) != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	CryptoTypes::Signature signature{};
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH1_SIGN_BEGIN");
#endif
	error = Interface::Crypto::GenerateSignature(authenticationData.data(),
						     authenticationData.size(), signature);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH1_SIGN_RESULT status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	ConstData readerCertificate{};
	if (Interface::Reader::IsCertificateProvisioned()) {
		error = Interface::Reader::GetCertificate(readerCertificate);
		if (error != ALIRO_NO_ERROR) {
			return error;
		}
	}
	size_t commandLength = 0;
	if (ultrawidelock_cred_build_auth1_command_ex(kAuth1PublicKeyRequested, signature.data(),
					     readerCertificate.mData, readerCertificate.mLength,
					     session.mTxBuffer.data(), session.mTxBuffer.size(),
					     &commandLength) != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH1_BUILD len=%zu certificate_len=%zu", commandLength,
		readerCertificate.mLength);
#endif
	session.mState = SessionState::AwaitingAuth1;
	return SendApCommand(session, session.mTxBuffer.data(), commandLength);
}

/**
 * Constructs a 12-byte nonce with a device/reader flag in byte 7 and a big-endian counter in bytes
 * 8–11.
 */
CryptoTypes::Nonce MakeNonce(bool device, uint32_t counter)
{
	CryptoTypes::Nonce nonce{};
	nonce[7] = device ? 1 : 0;
	nonce[8] = static_cast<uint8_t>(counter >> 24);
	nonce[9] = static_cast<uint8_t>(counter >> 16);
	nonce[10] = static_cast<uint8_t>(counter >> 8);
	nonce[11] = static_cast<uint8_t>(counter);
	return nonce;
}

/**
 * Derives the directional BLE session keys (BleSKReader and BleSKDevice) from the BLE key using
 * protocol versions and standard info labels; returns an error if key derivation fails.
 */
AliroError DeriveBleSessionKeys(SessionContext &session)
{
	std::array<uint8_t, (std::size(kBleProtocolVersions) + 1) * sizeof(ProtocolVersion)> salt{};
	size_t offset = 0;
	for (ProtocolVersion version : kBleProtocolVersions) {
		salt[offset++] = static_cast<uint8_t>(version >> 8);
		salt[offset++] = static_cast<uint8_t>(version);
	}
	salt[offset++] = static_cast<uint8_t>(session.mProtocolVersion >> 8);
	salt[offset++] = static_cast<uint8_t>(session.mProtocolVersion);
	static constexpr uint8_t kReaderInfo[]{'B', 'l', 'e', 'S', 'K', 'R',
					       'e', 'a', 'd', 'e', 'r'};
	static constexpr uint8_t kDeviceInfo[]{'B', 'l', 'e', 'S', 'K', 'D',
					       'e', 'v', 'i', 'c', 'e'};
	AliroError error = Interface::Crypto::DeriveSymmetricKey(session.mBleKeyId, kReaderInfo,
								 sizeof(kReaderInfo), salt.data(),
								 offset, session.mBleReaderKeyId);
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::DeriveSymmetricKey(session.mBleKeyId, kDeviceInfo,
							      sizeof(kDeviceInfo), salt.data(),
							      offset, session.mBleDeviceKeyId);
	}
	if (error == ALIRO_NO_ERROR) {
		session.mBleReaderCounter = kInitialCounter;
		session.mBleDeviceCounter = kInitialCounter;
	}
	return error;
}

/**
 * Encrypts a BLE message by parsing the plaintext header, encrypting the payload with the reader
 * counter and reader key, constructing the protected frame, sending it, and observing the timeout
 * message; returns an error if the key is absent, counter overflows, or encryption fails.
 */
AliroError EncryptBleMessage(SessionContext &session, const uint8_t *plaintext,
			     size_t plaintextLength)
{
	if (session.mBleReaderKeyId == 0 || session.mBleReaderCounter >= 0xffff ||
	    plaintext == nullptr) {
		return session.mBleReaderCounter >= 0xffff ? ALIRO_ENCRYPTION_COUNTER_OVERFLOW
							   : ALIRO_INVALID_STATE;
	}
	struct ultrawidelock_cred_ble_message message;
	size_t consumed = 0;
	if (ultrawidelock_cred_ble_parse_message(plaintext, plaintextLength, &message, &consumed) !=
		    ULTRAWIDELOCK_CRED_BLE_OK ||
	    consumed != plaintextLength ||
	    message.payload_length + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE > UINT16_MAX) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	uint8_t aad[ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE] = {
		message.protocol,
		message.message_id,
		static_cast<uint8_t>(message.payload_length >> 8),
		static_cast<uint8_t>(message.payload_length),
	};
	session.mBleBuffer[0] = message.protocol;
	session.mBleBuffer[1] = message.message_id;
	const size_t protectedLength = message.payload_length + ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE;
	session.mBleBuffer[2] = static_cast<uint8_t>(protectedLength >> 8);
	session.mBleBuffer[3] = static_cast<uint8_t>(protectedLength);
	CryptoTypes::AuthenticationTag tag{};
	const CryptoTypes::Nonce nonce = MakeNonce(false, session.mBleReaderCounter);
	AliroError error = Interface::Crypto::AeadEncrypt(
		session.mBleReaderKeyId, message.payload, message.payload_length, aad, sizeof(aad),
		nonce, session.mBleBuffer.data() + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE, tag);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	std::copy(tag.begin(), tag.end(),
		  session.mBleBuffer.begin() + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + message.payload_length);
	++session.mBleReaderCounter;
	error = Interface::Session::Send(
		*session.mHandle,
		{session.mBleBuffer.data(), ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + protectedLength});
	if (error == ALIRO_NO_ERROR) {
		ObserveResponseTimeoutMessage(session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_OUTGOING, plaintext,
					      plaintextLength);
	}
	return error;
}

/**
 * Decrypts a BLE message using the device key and current device counter as a nonce, validates the
 * authentication tag, increments the counter, and reconstructs the plaintext with the BLE header;
 * returns an error if the key is absent, counter overflows, or decryption fails.
 */
AliroError DecryptBleMessage(SessionContext &session,
			     const struct ultrawidelock_cred_ble_message &message,
			     uint8_t *plaintext, size_t plaintextCapacity, size_t &plaintextLength)
{
	if (session.mBleDeviceKeyId == 0 || session.mBleDeviceCounter >= 0xffff ||
	    message.payload_length <= ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE || plaintext == nullptr) {
		return session.mBleDeviceCounter >= 0xffff ? ALIRO_DECRYPTION_COUNTER_OVERFLOW
							   : ALIRO_INVALID_DATA_FORMAT;
	}
	const size_t payloadLength = message.payload_length - ULTRAWIDELOCK_CRED_BLE_AUTH_TAG_SIZE;
	if (plaintextCapacity < ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + payloadLength) {
		return ALIRO_NO_MEMORY;
	}
	uint8_t aad[ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE] = {
		message.protocol,
		message.message_id,
		static_cast<uint8_t>(payloadLength >> 8),
		static_cast<uint8_t>(payloadLength),
	};
	size_t decryptedLength = plaintextCapacity - ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE;
	const CryptoTypes::Nonce nonce = MakeNonce(true, session.mBleDeviceCounter);
	AliroError error = Interface::Crypto::AeadDecrypt(
		session.mBleDeviceKeyId, message.payload, message.payload_length, aad, sizeof(aad),
		nonce, plaintext + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE, decryptedLength);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	if (decryptedLength != payloadLength) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	plaintext[0] = message.protocol;
	plaintext[1] = message.message_id;
	plaintext[2] = static_cast<uint8_t>(payloadLength >> 8);
	plaintext[3] = static_cast<uint8_t>(payloadLength);
	plaintextLength = ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + payloadLength;
	++session.mBleDeviceCounter;
	return ALIRO_NO_ERROR;
}

/**
 * Encrypt and send an exchange command (0x80c9) with the given plaintext, reader key, and counter
 * state.
 *
 * Fails if plaintext is null, exceeds 254 bytes (to fit tag), or command length overflows the TX
 * buffer. Increments reader counter and sets session state to AwaitingExchangeResponse. Caller must
 * ensure key ID and counter state are correct for the current protocol phase (expedited or
 * step-up).
 */
AliroError SendEncryptedExchange(SessionContext &session, const uint8_t *plaintext,
				 size_t plaintextLength, CryptoTypes::KeyId readerKeyId,
				 bool useStepUpKeys)
{
	if (plaintext == nullptr ||
	    plaintextLength > UINT8_MAX - CryptoTypes::kAuthenticationTagLength) {
		return ALIRO_INVALID_ARGUMENT;
	}
	const size_t protectedLength = plaintextLength + CryptoTypes::kAuthenticationTagLength;
	const size_t commandLength = 5 + protectedLength + 1;
	if (commandLength > session.mTxBuffer.size()) {
		return ALIRO_NO_MEMORY;
	}

	uint8_t *command = session.mTxBuffer.data();
	const CryptoTypes::Nonce nonce = MakeNonce(false, session.mReaderCounter);
	CryptoTypes::AuthenticationTag tag{};
	AliroError error = Interface::Crypto::AeadEncrypt(readerKeyId, plaintext, plaintextLength,
							  nullptr, 0, nonce, command + 5, tag);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	++session.mReaderCounter;
	command[0] = 0x80;
	command[1] = 0xc9;
	command[2] = 0;
	command[3] = 0;
	command[4] = static_cast<uint8_t>(protectedLength);
	std::copy(tag.begin(), tag.end(), command + 5 + plaintextLength);
	command[commandLength - 1] = 0;
	session.mExchangeUsesStepUpKeys = useStepUpKeys;
	session.mState = SessionState::AwaitingExchangeResponse;
	return SendApCommand(session, command, commandLength);
}

/**
 * Send a URSK exchange command (0x98 0x00) using the expedited reader key without requesting
 * step-up.
 *
 * Wrapper around SendEncryptedExchange. Used to signal unlock completion when no Access Document is
 * needed.
 */
AliroError SendUrskExchange(SessionContext &session)
{
	static constexpr uint8_t plaintext[]{0x98, 0x00};
	return SendEncryptedExchange(session, plaintext, sizeof(plaintext),
				     session.mExpeditedReaderKeyId, false);
}

/**
 * Send a final NFC completion exchange (0x97 0x02 0x01 0x00) in the expedited or step-up phase,
 * matching the reference reader's successful secure state.
 *
 * Wrapper around SendEncryptedExchange. Caller specifies which reader key (expedited or step-up)
 * and phase to use via useStepUpKeys.
 */
AliroError SendNfcCompletionExchange(SessionContext &session, bool useStepUpKeys)
{
	/* Table 8-15/8-18: NFC transactions end with Reader Status. Match the
	 * reference reader's successful, currently-secure state indication. */
	static constexpr uint8_t plaintext[]{0x97, 0x02, 0x01, 0x00};
	const CryptoTypes::KeyId readerKeyId =
		useStepUpKeys ? session.mStepUpReaderKeyId : session.mExpeditedReaderKeyId;
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE NFC_COMPLETION_SEND phase=%s",
		useStepUpKeys ? "step-up" : "expedited");
#endif
	return SendEncryptedExchange(session, plaintext, sizeof(plaintext), readerKeyId,
				     useStepUpKeys);
}

AliroError StartStepUpExchange(SessionContext &session);

/**
 * Invokes the appropriate access processing method (fast or standard) based on the session state,
 * marks access as processed on success, and returns the operation status.
 */
AliroError ProcessAccess(SessionContext &session)
{
	if (session.mAccessProcessed) {
		return ALIRO_NO_ERROR;
	}
	AliroError error =
		session.mFastAccess
			? Interface::Access::ProcessAccessRequest(*session.mHandle,
								  session.mMatchedKpersistentKeyId)
			: Interface::Access::ProcessAccessRequest(*session.mHandle,
								  session.mCredentialPublicKey,
								  session.mKpersistentKeyId);
	if (error == ALIRO_NO_ERROR) {
		session.mAccessProcessed = true;
	}
	return error;
}

/**
 * Processes access, derives BLE session keys, extracts the ranging session ID from the transaction
 * identifier, starts the ranging session, encrypts and sends an access-completed message, destroys
 * Access Protocol keys, and transitions to UwbRanging on success.
 */
AliroError CompleteBleAccess(SessionContext &session)
{
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE COMPLETE_BLE_BEGIN processed=%u",
		static_cast<unsigned int>(session.mAccessProcessed));
#endif
	AliroError error = ProcessAccess(session);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE COMPLETE_BLE_ACCESS status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	error = DeriveBleSessionKeys(session);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE COMPLETE_BLE_KEYS status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	const size_t transactionOffset = session.mTransactionIdentifier.size() - sizeof(uint32_t);
	const uint32_t rangingSessionId =
		(static_cast<uint32_t>(session.mTransactionIdentifier[transactionOffset]) << 24) |
		(static_cast<uint32_t>(session.mTransactionIdentifier[transactionOffset + 1])
		 << 16) |
		(static_cast<uint32_t>(session.mTransactionIdentifier[transactionOffset + 2])
		 << 8) |
		session.mTransactionIdentifier[transactionOffset + 3];
	error = Interface::Session::StartRangingSession(*session.mHandle, rangingSessionId,
							session.mUrsk, session.mProtocolVersion);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE COMPLETE_BLE_RANGING status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	uint8_t completed[8];
	/* Report only to the User Device that caused a state change (Table 11-22,
	 * value 2 in B15:B13). Readers boot secured in the reference application. */
	if (ultrawidelock_cred_ble_build_access_completed(0x40,
						 static_cast<uint8_t>(ReaderStateByte::Secured),
						 completed) != ULTRAWIDELOCK_CRED_BLE_OK) {
		return ALIRO_ERROR_INTERNAL;
	}
	error = EncryptBleMessage(session, completed, sizeof(completed));
	if (error == ALIRO_NO_ERROR) {
		session.mState = SessionState::UwbRanging;
		/* Access Protocol keys are no longer used after the completed status.
		 * The UWB adapter has copied URSK; retain only the directional BleSKs. */
		DestroyKey(session.mReaderEphemeralKeyId);
		DestroyKey(session.mKdhKeyId);
		DestroyKey(session.mExpeditedReaderKeyId);
		DestroyKey(session.mExpeditedDeviceKeyId);
		DestroyKey(session.mStepUpKeyId);
		DestroyKey(session.mStepUpReaderKeyId);
		DestroyKey(session.mStepUpDeviceKeyId);
		DestroyKey(session.mBleKeyId);
		DestroyKey(session.mKpersistentKeyId);
		std::fill(session.mUrsk.begin(), session.mUrsk.end(), 0);
	}
	return error;
}

/**
 * Decrypt and validate an encrypted exchange response (expedited or step-up phase); optionally
 * request Access Document via step-up or complete the access.
 *
 * Fails on APDU status, decryption, or plaintext validation errors. Expects plaintext 0x00 0x02
 * 0x00 0x00 (success marker). If NFC, sets state to AccessComplete. If BLE with step-up requested,
 * calls StartStepUpExchange; otherwise completes access. Caller must have set the correct device
 * key ID and counter state before calling.
 */
AliroError HandleExchangeResponse(SessionContext &session, Data data)
{
	if (data.mLength < CryptoTypes::kAuthenticationTagLength + 2 ||
	    data.mData[data.mLength - 2] != 0x90 || data.mData[data.mLength - 1] != 0) {
		return ALIRO_APDU_STATUS_INVALID;
	}
	size_t plaintextLength = session.mPlaintextBuffer.size();
	const CryptoTypes::Nonce nonce = MakeNonce(true, session.mDeviceCounter);
	const CryptoTypes::KeyId deviceKeyId = session.mExchangeUsesStepUpKeys
						       ? session.mStepUpDeviceKeyId
						       : session.mExpeditedDeviceKeyId;
	AliroError error = Interface::Crypto::AeadDecrypt(
		deviceKeyId, data.mData, data.mLength - 2, nullptr, 0, nonce,
		session.mPlaintextBuffer.data(), plaintextLength);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	++session.mDeviceCounter;
	static constexpr uint8_t success[]{0x00, 0x02, 0x00, 0x00};
	if (plaintextLength != sizeof(success) ||
	    !std::equal(std::begin(success), std::end(success), session.mPlaintextBuffer.begin())) {
		return ALIRO_INVALID_DATA_CONTENT;
	}
	if (!session.mHandle->IsBle()) {
		session.mState = SessionState::AccessComplete;
		return ALIRO_NO_ERROR;
	}
	if (session.mRequestedElementLength != 0) {
		return StartStepUpExchange(session);
	}
	return CompleteBleAccess(session);
}

/**
 * Build and send the next envelope command in a step-up exchange, segmenting mExchangeBuffer across
 * one or more APDUs.
 *
 * Fails if segmentation fails or buffer is too small. Updates mExchangeOffset and mLastEnvelope;
 * sets state to SendingStepUpEnvelope. Caller must have populated mExchangeBuffer, mMaxCommandData,
 * and mMaxResponseData before calling.
 */
AliroError SendNextEnvelope(SessionContext &session)
{
	size_t commandLength = 0;
	if (ultrawidelock_stepup_build_envelope_ex(
		    session.mExchangeBuffer.data(), session.mExchangeLength,
		    &session.mExchangeOffset, session.mMaxCommandData, session.mMaxResponseData,
		    session.mUseExtendedApdus, session.mTxBuffer.data(), session.mTxBuffer.size(),
		    &commandLength, &session.mLastEnvelope) != ULTRAWIDELOCK_STEPUP_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_ENVELOPE_TX len=%zu offset=%zu total=%zu "
		"last=%u max_command=%zu max_response=%zu extended=%u",
		commandLength, session.mExchangeOffset, session.mExchangeLength,
		session.mLastEnvelope ? 1u : 0u, session.mMaxCommandData, session.mMaxResponseData,
		session.mUseExtendedApdus ? 1u : 0u);
	LOG_HEXDUMP_INF(session.mTxBuffer.data(), commandLength,
			"ULTRAWIDELOCK_TRACE STEP_UP_ENVELOPE_TX bytes:");
#endif
	session.mState = SessionState::SendingStepUpEnvelope;
	return SendApCommand(session, session.mTxBuffer.data(), commandLength);
}

/**
 * Request the next chunk of a step-up response from the reader via GET RESPONSE command.
 *
 * Fails if expectedLength cannot be encoded or buffer is too small. Sets session state to
 * AwaitingStepUpResponse. Used when a single APDU response is insufficient to deliver the full
 * step-up plaintext.
 */
AliroError SendGetResponse(SessionContext &session, size_t expectedLength)
{
	size_t commandLength = 0;
	if (ultrawidelock_stepup_build_get_response_ex(expectedLength, session.mTxBuffer.data(),
					       session.mTxBuffer.size(), &commandLength) != 0) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	session.mState = SessionState::AwaitingStepUpResponse;
	return SendApCommand(session, session.mTxBuffer.data(), commandLength);
}

/**
 * Derive directional step-up keys, build and encrypt a device request, wrap it in session data and
 * DO53, then send the first envelope segment.
 *
 * Fails if key derivation, request building, encryption, wrapping, or segmentation fails. Resets
 * reader and device counters to initial value. Sets state to SendingStepUpEnvelope. Caller must
 * have populated mRequestedElement, mRequestedElementLength, and mIntentToStore before calling. On
 * trace enabled, performs AEAD round-trip verification.
 */
AliroError StartStepUpExchange(SessionContext &session)
{
	/* Derive the directional keys only when the step-up phase actually starts.
	 * In NFC this is deliberately after the dedicated step-up AID has been
	 * selected, matching the reference stack's key lifetime and ordering. */
	static constexpr uint8_t kSkReaderInfo[]{'S', 'K', 'R', 'e', 'a', 'd', 'e', 'r'};
	static constexpr uint8_t kSkDeviceInfo[]{'S', 'K', 'D', 'e', 'v', 'i', 'c', 'e'};
	AliroError error = Interface::Crypto::DeriveSymmetricKey(
		session.mStepUpKeyId, kSkReaderInfo, sizeof(kSkReaderInfo), nullptr, 0,
		session.mStepUpReaderKeyId);
	if (error == ALIRO_NO_ERROR) {
		error = Interface::Crypto::DeriveSymmetricKey(session.mStepUpKeyId, kSkDeviceInfo,
							      sizeof(kSkDeviceInfo), nullptr, 0,
							      session.mStepUpDeviceKeyId);
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_KEYS status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	/* Step-up has independent directional counters. */
	session.mReaderCounter = kInitialCounter;
	session.mDeviceCounter = kInitialCounter;
	size_t requestLength = 0;
	if (ultrawidelock_stepup_build_device_request_ex(
		    session.mRequestedElement.data(), session.mRequestedElementLength,
		    session.mIntentToStore, session.mPlaintextBuffer.data(),
		    session.mPlaintextBuffer.size(), &requestLength) != 0) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_HEXDUMP_INF(session.mPlaintextBuffer.data(), requestLength,
			"ULTRAWIDELOCK_TRACE SOURCE STEP_UP_DEVICE_REQUEST_PLAINTEXT");
#endif
	const CryptoTypes::Nonce nonce = MakeNonce(false, session.mReaderCounter);
	CryptoTypes::AuthenticationTag tag{};
	error = Interface::Crypto::AeadEncrypt(
		session.mStepUpReaderKeyId, session.mPlaintextBuffer.data(), requestLength, nullptr,
		0, nonce, session.mSessionDataBuffer.data(), tag);
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	++session.mReaderCounter;
	if (requestLength + tag.size() > session.mSessionDataBuffer.size()) {
		return ALIRO_NO_MEMORY;
	}
	std::copy(tag.begin(), tag.end(), session.mSessionDataBuffer.begin() + requestLength);

#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	/* Verify the exact ciphertext/tag that will be sent.  This isolates a local
	 * AES-GCM generation problem from an APDU or phone-side rejection without
	 * exposing key material in the trace.  mTxBuffer is scratch at this point
	 * and is overwritten when the ENVELOPE command is built below. */
	size_t verifiedLength = session.mTxBuffer.size();
	const AliroError verifyError = Interface::Crypto::AeadDecrypt(
		session.mStepUpReaderKeyId, session.mSessionDataBuffer.data(),
		requestLength + tag.size(), nullptr, 0, nonce, session.mTxBuffer.data(),
		verifiedLength);
	const bool verified = verifyError == ALIRO_NO_ERROR && verifiedLength == requestLength &&
			      std::equal(session.mPlaintextBuffer.begin(),
					 session.mPlaintextBuffer.begin() + requestLength,
					 session.mTxBuffer.begin());
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_AEAD_SELF_CHECK status=%d "
		"decrypted_len=%zu expected_len=%zu match=%u",
		verifyError.ToInt(), verifiedLength, requestLength, verified ? 1u : 0u);
#endif

	size_t sessionDataLength = 0;
	if (ultrawidelock_stepup_wrap_sessiondata_raw(session.mSessionDataBuffer.data(),
					      requestLength + tag.size(),
					      session.mPlaintextBuffer.data(),
					      session.mPlaintextBuffer.size(),
					      &sessionDataLength) != 0 ||
	    ultrawidelock_stepup_wrap_do53(session.mPlaintextBuffer.data(), sessionDataLength,
				   session.mExchangeBuffer.data(), session.mExchangeBuffer.size(),
				   &session.mExchangeLength) != 0) {
		return ALIRO_NO_MEMORY;
	}
	session.mExchangeOffset = 0;
	session.mResponseLength = 0;
	return SendNextEnvelope(session);
}

/**
 * Encodes the CBOR byte-string header for a given length into output, returning the number of bytes
 * written (1, 2, or 3 depending on the length value).
 */
size_t EncodeBstrHead(size_t length, uint8_t *output)
{
	if (length < 24) {
		output[0] = static_cast<uint8_t>(0x40 | length);
		return 1;
	}
	if (length <= 0xff) {
		output[0] = 0x58;
		output[1] = static_cast<uint8_t>(length);
		return 2;
	}
	output[0] = 0x59;
	output[1] = static_cast<uint8_t>(length >> 8);
	output[2] = static_cast<uint8_t>(length);
	return 3;
}

/**
 * The Access-Document field shape this session layer consumes, mapped from the shared
 * ultrawidelock_stepup_parse.c decode. All pointers are slices into the parse scratch / response
 * buffer.
 */
struct AccessDocumentView {
	const uint8_t *device_public_key; /* 65-byte uncompressed point */
	const uint8_t *data_element;      /* raw encoded elementValue item */
	size_t data_element_length;
	const uint8_t *issuer_signed_item; /* 24(bstr(IssuerSignedItem)) bytes */
	size_t issuer_signed_item_length;
	const uint8_t *expected_digest; /* 32-byte valueDigest for the item */
	const uint8_t *cose_protected;
	size_t cose_protected_length;
	const uint8_t *cose_payload;
	size_t cose_payload_length;
	const uint8_t *cose_signature; /* 64-byte r||s */
	const uint8_t *issuer_kid;     /* nullptr when absent */
	size_t issuer_kid_length;
	const uint8_t *issuer_certificate; /* nullptr when absent */
	size_t issuer_certificate_length;
	const uint8_t *signed_timestamp; /* 20-char tdate slices */
	const uint8_t *valid_from;
	const uint8_t *valid_until;
	bool time_verification_required;
	bool has_validity_iteration;
	uint64_t validity_iteration;
};

/**
 * Map a parsed DeviceResponse onto the fields this layer consumes, re-imposing the structural
 * requirements the former dedicated parser enforced: version 1.0, status 0, docType/MSO docType
 * "aliro-a", SHA-256 digests, ES256 protected header, a kid of at most 8 bytes and/or an x5chain
 * end-entity certificate, an EC2/P-256 deviceKey, the requested element with its valueDigest, and
 * complete validity timestamps. Returns 0 on success, -1 on any missing or mismatched field.
 */
int MapAccessDocument(const struct ultrawidelock_stepup_doc &doc, const uint8_t *requested,
		      size_t requestedLength, AccessDocumentView &out)
{
	if (!doc.have_document || doc.status != 0 ||
	    std::strcmp(doc.version, "1.0") != 0 ||
	    std::strcmp(doc.doc_type, ULTRAWIDELOCK_STEPUP_DOCTYPE_ACCESS) != 0 ||
	    std::strcmp(doc.mso_doc_type, ULTRAWIDELOCK_STEPUP_DOCTYPE_ACCESS) != 0 ||
	    std::strcmp(doc.name_space, ULTRAWIDELOCK_STEPUP_DOCTYPE_ACCESS) != 0 ||
	    std::strcmp(doc.digest_alg, "SHA-256") != 0) {
		return -1;
	}
	const struct ultrawidelock_stepup_item *item = nullptr;
	for (size_t i = 0; i < doc.n_items; ++i) {
		if (std::strlen(doc.items[i].elem_id) == requestedLength &&
		    std::memcmp(doc.items[i].elem_id, requested, requestedLength) == 0) {
			item = &doc.items[i];
			break;
		}
	}
	if (item == nullptr || item->value == nullptr) {
		return -1;
	}
	const struct ultrawidelock_stepup_digest *digest = nullptr;
	for (size_t i = 0; i < doc.n_digests; ++i) {
		if (doc.digests[i].id == item->digest_id) {
			digest = &doc.digests[i];
			break;
		}
	}
	if (digest == nullptr || doc.protected_hdr == nullptr || doc.payload == nullptr ||
	    doc.signature == nullptr || !doc.have_cose_alg || doc.cose_alg != -7) {
		return -1;
	}
	if (doc.kid != nullptr && (doc.kid_len == 0 || doc.kid_len > 8)) {
		return -1;
	}
	if (doc.x5chain != nullptr && doc.x5_cert == nullptr) {
		return -1;
	}
	if (doc.kid == nullptr && doc.x5_cert == nullptr) {
		return -1;
	}
	if (!doc.have_device_key || doc.signed_raw == nullptr || doc.valid_from_raw == nullptr ||
	    doc.valid_until_raw == nullptr || !doc.have_time_verification_required) {
		return -1;
	}
	out.device_public_key = doc.device_key;
	out.data_element = item->value;
	out.data_element_length = item->value_len;
	out.issuer_signed_item = item->tagged;
	out.issuer_signed_item_length = item->tagged_len;
	out.expected_digest = digest->hash;
	out.cose_protected = doc.protected_hdr;
	out.cose_protected_length = doc.protected_len;
	out.cose_payload = doc.payload;
	out.cose_payload_length = doc.payload_len;
	out.cose_signature = doc.signature;
	out.issuer_kid = doc.kid;
	out.issuer_kid_length = doc.kid_len;
	out.issuer_certificate = doc.x5_cert;
	out.issuer_certificate_length = doc.x5_cert_len;
	out.signed_timestamp = doc.signed_raw;
	out.valid_from = doc.valid_from_raw;
	out.valid_until = doc.valid_until_raw;
	out.time_verification_required = doc.time_verification_required != 0;
	out.has_validity_iteration = doc.have_iteration != 0;
	out.validity_iteration = doc.iteration;
	return 0;
}

/**
 * Parses and validates an access document: verifies the issuer-signed item digest, validates the
 * issuer certificate or key ID, checks the document's validity period, ensures the device public
 * key matches, constructs a COSE Sig_structure, verifies the signature, and invokes the access
 * processing interface.
 */
AliroError ValidateAndProcessAccessDocument(SessionContext &session, const uint8_t *deviceResponse,
					    size_t deviceResponseLength)
{
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_VALIDATE_BEGIN len=%zu requested_len=%zu",
		deviceResponseLength, session.mRequestedElementLength);
	static bool tracedDocument;
	if (!tracedDocument) {
		tracedDocument = true;
		LOG_HEXDUMP_INF(deviceResponse, deviceResponseLength,
				"ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_PLAINTEXT");
	}
#endif
	/* Parse scratch: the full DeviceResponse decode is a few KiB of slices and
	 * digests, too large for the protocol worker stack. All session APDU
	 * processing runs on one context, so a single static scratch is safe. */
	static struct ultrawidelock_stepup_doc stepUpDoc;
	AccessDocumentView parsed;
	int parseStatus = ultrawidelock_stepup_parse_response(deviceResponse, deviceResponseLength,
						      &stepUpDoc);
	if (parseStatus == 0) {
		parseStatus = MapAccessDocument(stepUpDoc, session.mRequestedElement.data(),
						session.mRequestedElementLength, parsed);
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_PARSE status=%d", parseStatus);
#endif
	if (parseStatus != 0) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	CryptoTypes::Sha256Hash digest{};
	AliroError error = Interface::Crypto::Sha256(parsed.issuer_signed_item,
						     parsed.issuer_signed_item_length, digest);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_DIGEST status=%d match=%u", error.ToInt(),
		static_cast<unsigned int>(
			error == ALIRO_NO_ERROR &&
			std::equal(digest.begin(), digest.end(), parsed.expected_digest)));
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	if (!std::equal(digest.begin(), digest.end(), parsed.expected_digest)) {
		return ALIRO_INVALID_SIGNATURE;
	}

	CryptoTypes::PublicKey issuerPublicKey{};
	bool issuerKeyAvailable = false;
	if (parsed.issuer_certificate != nullptr) {
		std::optional<Interface::CredentialIssuerCertificate::CertificateTimestamps>
			certificateTimes;
		ConstData certificate{parsed.issuer_certificate, parsed.issuer_certificate_length};
		error = Interface::CredentialIssuerCertificate::Validate(
			certificate, issuerPublicKey, certificateTimes);
		if (error != ALIRO_NO_ERROR) {
			return error;
		}
		issuerKeyAvailable = true;
		if (certificateTimes.has_value()) {
			auto certificateValid = Interface::AccessDocument::VerifyValidityPeriod(
				certificateTimes->mValidFrom, certificateTimes->mValidUntil);
			if (!certificateValid.has_value() || !*certificateValid) {
				return ALIRO_PUBLIC_KEY_EXPIRED;
			}
		}
	}
	if (parsed.issuer_kid != nullptr) {
		CryptoTypes::KeyIdentifier kid{};
		std::copy_n(parsed.issuer_kid, parsed.issuer_kid_length, kid.begin());
		CryptoTypes::PublicKey storedIssuerPublicKey{};
		error = Interface::Access::GetCredentialIssuerPublicKey(kid, storedIssuerPublicKey);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
		LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_ISSUER_LOOKUP status=%d",
			error.ToInt());
#endif
		if (error != ALIRO_NO_ERROR) {
			return error;
		}
		if (issuerKeyAvailable && storedIssuerPublicKey != issuerPublicKey) {
			return ALIRO_PUBLIC_KEY_NOT_TRUSTED;
		}
		issuerPublicKey = storedIssuerPublicKey;
		issuerKeyAvailable = true;
	}
	if (!issuerKeyAvailable) {
		return ALIRO_PUBLIC_KEY_NOT_FOUND;
	}

	/* RFC 9052 Sig_structure = ["Signature1", protected, bstr(), payload]. */
	size_t sigLength = 0;
	auto append = [&](const uint8_t *bytes, size_t length) {
		if (length > session.mSessionDataBuffer.size() - sigLength) {
			return false;
		}
		std::copy_n(bytes, length, session.mSessionDataBuffer.begin() + sigLength);
		sigLength += length;
		return true;
	};
	static constexpr uint8_t prefix[]{0x84, 0x6a, 'S', 'i', 'g', 'n',
					  'a',  't',  'u', 'r', 'e', '1'};
	uint8_t head[3];
	if (!append(prefix, sizeof(prefix))) {
		return ALIRO_NO_MEMORY;
	}
	size_t headLength = EncodeBstrHead(parsed.cose_protected_length, head);
	if (!append(head, headLength) ||
	    !append(parsed.cose_protected, parsed.cose_protected_length)) {
		return ALIRO_NO_MEMORY;
	}
	static constexpr uint8_t emptyBstr = 0x40;
	if (!append(&emptyBstr, 1)) {
		return ALIRO_NO_MEMORY;
	}
	headLength = EncodeBstrHead(parsed.cose_payload_length, head);
	if (!append(head, headLength) || !append(parsed.cose_payload, parsed.cose_payload_length)) {
		return ALIRO_NO_MEMORY;
	}
	CryptoTypes::Signature signature{};
	std::copy_n(parsed.cose_signature, signature.size(), signature.begin());
	error = Interface::Crypto::VerifySignature(
		issuerPublicKey, session.mSessionDataBuffer.data(), sigLength, signature);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_SIGNATURE status=%d sig_structure_len=%zu",
		error.ToInt(), sigLength);
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}

	Timestamp signedTimestamp{}, validFromTimestamp{}, validUntilTimestamp{};
	std::copy_n(parsed.signed_timestamp, signedTimestamp.size(), signedTimestamp.begin());
	std::copy_n(parsed.valid_from, validFromTimestamp.size(), validFromTimestamp.begin());
	std::copy_n(parsed.valid_until, validUntilTimestamp.size(), validUntilTimestamp.begin());
	auto validFrom = Time::FromTimestamp(validFromTimestamp);
	auto validUntil = Time::FromTimestamp(validUntilTimestamp);
	if (!validFrom.has_value() || !validUntil.has_value()) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	auto valid = Interface::AccessDocument::VerifyValidityPeriod(*validFrom, *validUntil);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_VALIDITY known=%u valid=%u required=%u",
		static_cast<unsigned int>(valid.has_value()),
		static_cast<unsigned int>(valid.value_or(false)),
		static_cast<unsigned int>(parsed.time_verification_required));
#endif
	if ((valid.has_value() && !*valid) ||
	    (!valid.has_value() && parsed.time_verification_required)) {
		return ALIRO_PUBLIC_KEY_EXPIRED;
	}
	CryptoTypes::PublicKey devicePublicKey{};
	std::copy_n(parsed.device_public_key, devicePublicKey.size(), devicePublicKey.begin());
	if (devicePublicKey != session.mCredentialPublicKey) {
		return ALIRO_PUBLIC_KEY_NOT_TRUSTED;
	}
	ConstData element{parsed.data_element, parsed.data_element_length};
	std::optional<uint64_t> iteration;
	if (parsed.has_validity_iteration) {
		iteration = parsed.validity_iteration;
	}
	AccessDocumentTypes::AccessDocument accessDocument{
		devicePublicKey, element, issuerPublicKey, signedTimestamp, iteration};
	error = Interface::Access::ProcessAccessRequest(*session.mHandle,
							session.mCredentialPublicKey,
							session.mKpersistentKeyId, accessDocument);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT_PROCESS status=%d", error.ToInt());
#endif
	return error;
}

/**
 * Unwraps the step-up response DO53 and session-data containers, decrypts the access document using
 * the device counter and StepUpDeviceKey, validates and processes the access document, and marks
 * access as processed on success.
 */
AliroError FinishStepUpResponse(SessionContext &session)
{
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_FINISH_BEGIN response_len=%zu",
		session.mResponseLength);
#endif
	const uint8_t *sessionData = nullptr;
	size_t sessionDataLength = 0;
	if (ultrawidelock_stepup_unwrap_do53(session.mExchangeBuffer.data(), session.mResponseLength,
				     &sessionData, &sessionDataLength) != 0) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	const uint8_t *ciphertext = nullptr;
	size_t ciphertextLength = 0;
	if (ultrawidelock_stepup_unwrap_sessiondata_raw(sessionData, sessionDataLength, &ciphertext,
						&ciphertextLength) != 0 ||
	    ciphertextLength < CryptoTypes::kAuthenticationTagLength) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_UNWRAP session_len=%zu ciphertext_len=%zu",
		sessionDataLength, ciphertextLength);
#endif
	size_t plaintextLength = session.mPlaintextBuffer.size();
	const CryptoTypes::Nonce nonce = MakeNonce(true, session.mDeviceCounter);
	AliroError error = Interface::Crypto::AeadDecrypt(
		session.mStepUpDeviceKeyId, ciphertext, ciphertextLength, nullptr, 0, nonce,
		session.mPlaintextBuffer.data(), plaintextLength);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_DECRYPT status=%d plaintext_len=%zu", error.ToInt(),
		plaintextLength);
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	++session.mDeviceCounter;
	error = ValidateAndProcessAccessDocument(session, session.mPlaintextBuffer.data(),
						 plaintextLength);
	if (error == ALIRO_NO_ERROR) {
		session.mAccessProcessed = true;
	}
	return error;
}

/**
 * Collect one or more envelope responses from the reader and assemble the complete step-up
 * plaintext.
 *
 * Fails if response collection fails or APDU status is non-zero. If more data needed, sends GET
 * RESPONSE. Otherwise decrypts and validates step-up response, then completes the access (BLE or
 * NFC). Sets state to AwaitingStepUpResponse or AccessComplete. Caller must have initialized
 * mExchangeBuffer before calling.
 */
AliroError CollectStepUpResponse(SessionContext &session, Data data)
{
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	if (data.mLength >= 2) {
		LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_APDU_RX len=%zu sw=%02x%02x", data.mLength,
			data.mData[data.mLength - 2], data.mData[data.mLength - 1]);
	} else {
		LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_APDU_RX len=%zu missing_status_word",
			data.mLength);
	}
	LOG_HEXDUMP_INF(data.mData, data.mLength, "ULTRAWIDELOCK_TRACE STEP_UP_APDU_RX bytes:");
#endif
	size_t nextLength = 0;
	const int result = ultrawidelock_stepup_collect_response(
		data.mData, data.mLength, session.mExchangeBuffer.data(),
		session.mExchangeBuffer.size(), &session.mResponseLength, &nextLength);
	if (result == ULTRAWIDELOCK_STEPUP_MORE_RESPONSE) {
		return SendGetResponse(session, nextLength);
	}
	if (result != ULTRAWIDELOCK_STEPUP_OK) {
		return ALIRO_APDU_STATUS_INVALID;
	}
	AliroError error = FinishStepUpResponse(session);
	if (error == ALIRO_NO_ERROR) {
		if (session.mHandle->IsBle()) {
			return CompleteBleAccess(session);
		}
		return SendNfcCompletionExchange(session, true);
	}
	return error;
}

/**
 * Decrypt, validate, and process Auth1 response; derive persistent key; optionally request Access
 * Document or start step-up exchange.
 *
 * Fails on APDU status, decryption, parse, signature verification, or persistent key derivation
 * errors. Extracts credential public key, verifies signature, and checks signaling bitmap for
 * Access Document and step-up AID selection requirements. Sets state to SelectingStepUp,
 * AwaitingAuth1 (BLE with document), or initiates step-up/completion. Caller must have set
 * mExpeditedDeviceKeyId and mDeviceCounter before calling.
 */
AliroError HandleAuth1Response(SessionContext &session, Data data)
{
	if (data.mLength < CryptoTypes::kAuthenticationTagLength + 2 ||
	    data.mData[data.mLength - 2] != 0x90 || data.mData[data.mLength - 1] != 0x00) {
		return ALIRO_APDU_STATUS_INVALID;
	}
	const size_t encryptedLength = data.mLength - 2;
	size_t plaintextLength = session.mPlaintextBuffer.size();
	const CryptoTypes::Nonce nonce = MakeNonce(true, session.mDeviceCounter);
	AliroError error = Interface::Crypto::AeadDecrypt(
		session.mExpeditedDeviceKeyId, data.mData, encryptedLength, nullptr, 0, nonce,
		session.mPlaintextBuffer.data(), plaintextLength);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH1_DECRYPT status=%d plaintext_len=%zu", error.ToInt(),
		plaintextLength);
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	++session.mDeviceCounter;

	ultrawidelock_cred_auth1_response response;
	if (ultrawidelock_cred_parse_auth1_plaintext(session.mPlaintextBuffer.data(),
						     plaintextLength, true,
						     &response) != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	CryptoTypes::PublicKey credentialPublicKey{};
	CryptoTypes::Signature signature{};
	std::copy_n(response.credential_public_key, credentialPublicKey.size(),
		    credentialPublicKey.begin());
	std::copy_n(response.signature, signature.size(), signature.begin());
	std::array<uint8_t, ULTRAWIDELOCK_CRED_AUTH_DATA_SIZE> authenticationData{};
	if (ultrawidelock_cred_build_authentication_data(
		    session.mReaderIdentifier.data(), session.mCredentialEphemeralPublicKey.data(),
		    session.mReaderEphemeralPublicKey.data(), session.mTransactionIdentifier.data(),
		    kDeviceAuthenticationUsage, authenticationData.data()) != ULTRAWIDELOCK_CRED_AUTH_OK) {
		return ALIRO_INVALID_DATA_FORMAT;
	}
	error = Interface::Crypto::VerifySignature(credentialPublicKey, authenticationData.data(),
						   authenticationData.size(), signature);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE AUTH1_VERIFY status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	error = DerivePersistentKey(session, credentialPublicKey);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE PERSISTENT_DERIVE status=%d", error.ToInt());
#endif
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	session.mCredentialPublicKey = credentialPublicKey;
	std::optional<Timestamp> credentialSignedTimestamp;
	if (response.credential_signed_timestamp != nullptr) {
		credentialSignedTimestamp.emplace();
		std::copy_n(response.credential_signed_timestamp, credentialSignedTimestamp->size(),
			    credentialSignedTimestamp->begin());
	}
	auto request = Interface::Access::GetAccessDocumentRequestParameters(
		credentialPublicKey, credentialSignedTimestamp);
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE ACCESS_DOCUMENT requested=%u signaling=0x%04x",
		static_cast<unsigned int>(request.has_value()), response.signaling_bitmap);
#endif
	/* Bit 0 advertises that an Access Document can be retrieved. If the
	 * application does not need one, finish expedited-standard directly. */
	if (!request.has_value() || (response.signaling_bitmap & 0x0001u) == 0) {
		if (session.mHandle->IsBle()) {
			return SendUrskExchange(session);
		}
		error = ProcessAccess(session);
		if (error != ALIRO_NO_ERROR) {
			return error;
		}
		return SendNfcCompletionExchange(session, false);
	}
	if (request->mElementIdentifier.mData == nullptr ||
	    request->mElementIdentifier.mLength == 0 ||
	    request->mElementIdentifier.mLength > session.mRequestedElement.size()) {
		return ALIRO_INVALID_ARGUMENT;
	}
	session.mRequestedElementLength = request->mElementIdentifier.mLength;
	std::copy_n(request->mElementIdentifier.mData, request->mElementIdentifier.mLength,
		    session.mRequestedElement.begin());
	session.mIntentToStore = request->mIntentToStore;
	if (session.mHandle->IsBle()) {
		return SendUrskExchange(session);
	}
	/* Bit 2 requires selecting the dedicated step-up AID in the same NFC
	 * session before the ISO 18013-5 ENVELOPE exchange. */
	if ((response.signaling_bitmap & 0x0004u) != 0) {
		if (ultrawidelock_cred_build_select_command(ULTRAWIDELOCK_CRED_SELECT_STEP_UP,
						   session.mTxBuffer.data()) !=
		    ULTRAWIDELOCK_CRED_SELECT_OK) {
			return ALIRO_ERROR_INTERNAL;
		}
		session.mState = SessionState::SelectingStepUp;
		return SendApCommand(session, session.mTxBuffer.data(),
				     ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE);
	}
	return StartStepUpExchange(session);
}

} // namespace

/**
 * Creates a new session for the given connection handle: for NFC, sends a Select command and
 * transitions to SelectingExpedited; for BLE, validates the protocol version and transitions to
 * BleConnected; returns an error if the session already exists, allocation fails, or the protocol
 * version is unsupported.
 */
AliroError AliroStack::CreateSession(ConnectionHandle connectionHandle)
{
	StackLock lock;
	if (FindSession(connectionHandle) != nullptr) {
		return ALIRO_INVALID_STATE;
	}
	SessionContext *session = AllocateSession(connectionHandle);
	if (session == nullptr) {
		return ALIRO_NO_MEMORY;
	}

	if (connectionHandle.IsNfc()) {
		if (ultrawidelock_cred_build_select_command(ULTRAWIDELOCK_CRED_SELECT_EXPEDITED,
						   session->mTxBuffer.data()) !=
		    ULTRAWIDELOCK_CRED_SELECT_OK) {
			ResetSession(*session);
			return ALIRO_ERROR_INTERNAL;
		}
		session->mState = SessionState::SelectingExpedited;
		AliroError error =
			Interface::Session::Send(connectionHandle, {session->mTxBuffer.data(),
								    ULTRAWIDELOCK_CRED_SELECT_COMMAND_SIZE});
		if (error != ALIRO_NO_ERROR) {
			ResetSession(*session);
		}
		return error;
	}

	session->mState = SessionState::BleConnected;
	session->mProtocolVersion = Interface::Ble::GetProtocolVersion(connectionHandle);
	if (session->mProtocolVersion != 0x0100) {
		ResetSession(*session);
		return ALIRO_VERSION_NOT_SUPPORTED;
	}
	return ALIRO_NO_ERROR;
}

/**
 * Finds the session for the given connection handle and destroys it if found; invokes the session
 * termination callback on success.
 */
void AliroStack::DestroySession(ConnectionHandle connectionHandle)
{
	bool found = false;
	{
		StackLock lock;
		SessionContext *session = FindSession(connectionHandle);
		if (session != nullptr) {
			ResetSession(*session);
			found = true;
		}
	}
	if (found) {
		Interface::Session::HandleTermination(connectionHandle);
	}
}

namespace
{

/**
 * Route incoming session data (NFC APDU or reassembled BLE frame) to the appropriate protocol
 * handler based on session state, forwarding UWB control to the UWB stack if needed.
 *
 * On BLE: reassembles fragmented messages, validates frames, decrypts, and routes to
 * auth/exchange/ranging handlers or timeout control. On NFC: dispatches to
 * Select/Auth0/Auth1/Exchange response handlers. Destroys session on frame error, parse error, or
 * explicit termination. Caller must ensure handle and data are valid; data may be null (triggers
 * session destruction).
 */
void ProcessSessionData(ConnectionHandle handle, Data data)
{
	if (data.mData == nullptr || data.mLength == 0) {
		AliroStack::Instance().DestroySession(handle);
		return;
	}

	auto processApResponse = [](SessionContext &session, Data response, bool &terminate) {
		AliroError status = ALIRO_NO_ERROR;
		if (session.mState == SessionState::SelectingExpedited) {
			ultrawidelock_cred_select_response selected;
			const int result = ultrawidelock_cred_parse_select_response_ex(
				response.mData, response.mLength, ULTRAWIDELOCK_CRED_SELECT_EXPEDITED,
				&selected);
			if (result != ULTRAWIDELOCK_CRED_SELECT_OK ||
			    selected.proprietary_information_tlv_length >
				    session.mProprietaryInformation.size()) {
				return AliroError(ALIRO_INVALID_DATA_FORMAT);
			}
			session.mProtocolVersion = selected.selected_protocol_version;
			session.mProprietaryInformationLength =
				selected.proprietary_information_tlv_length;
			ApplyNfcApduLimits(session, selected);
			std::copy_n(selected.proprietary_information_tlv,
				    selected.proprietary_information_tlv_length,
				    session.mProprietaryInformation.begin());
			return SendAuth0(session);
		}
		if (session.mState == SessionState::AwaitingAuth0) {
			status = HandleAuth0Response(session, response);
			terminate = session.mState == SessionState::AccessComplete;
		} else if (session.mState == SessionState::AwaitingAuth1) {
			status = HandleAuth1Response(session, response);
			terminate = session.mState == SessionState::AwaitingAuth1;
		} else if (session.mState == SessionState::AwaitingExchangeResponse) {
			status = HandleExchangeResponse(session, response);
			terminate = session.mState == SessionState::AccessComplete;
		} else if (session.mState == SessionState::SelectingStepUp) {
			ultrawidelock_cred_select_response selected;
			if (ultrawidelock_cred_parse_select_response_ex(response.mData, response.mLength,
							       ULTRAWIDELOCK_CRED_SELECT_STEP_UP,
							       &selected) != ULTRAWIDELOCK_CRED_SELECT_OK) {
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
				if (response.mLength >= 2) {
					LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_SELECT_RX parse=failed "
						"len=%zu "
						"sw=%02x%02x",
						response.mLength,
						response.mData[response.mLength - 2],
						response.mData[response.mLength - 1]);
				} else {
					LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_SELECT_RX parse=failed "
						"len=%zu "
						"missing_status_word",
						response.mLength);
				}
				LOG_HEXDUMP_INF(response.mData, response.mLength,
						"ULTRAWIDELOCK_TRACE STEP_UP_SELECT_RX bytes:");
#endif
				status = ALIRO_INVALID_DATA_FORMAT;
			} else {
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
				LOG_INF("ULTRAWIDELOCK_TRACE SOURCE STEP_UP_SELECT_RX parse=ok len=%zu "
					"max_command=%zu max_response=%zu extended_supported=%u",
					response.mLength, selected.max_command_data_length,
					selected.max_response_data_length,
					selected.extended_length_supported ? 1u : 0u);
				LOG_HEXDUMP_INF(response.mData, response.mLength,
						"ULTRAWIDELOCK_TRACE STEP_UP_SELECT_RX bytes:");
#endif
				ApplyNfcApduLimits(session, selected);
				status = StartStepUpExchange(session);
			}
		} else if (session.mState == SessionState::SendingStepUpEnvelope) {
			if (!session.mLastEnvelope) {
				status = response.mLength == 2 && response.mData[0] == 0x90 &&
							 response.mData[1] == 0
						 ? SendNextEnvelope(session)
						 : ALIRO_APDU_STATUS_INVALID;
			} else {
				session.mResponseLength = 0;
				status = CollectStepUpResponse(session, response);
				terminate = session.mState == SessionState::AccessComplete;
			}
		} else if (session.mState == SessionState::AwaitingStepUpResponse) {
			status = CollectStepUpResponse(session, response);
			terminate = session.mState == SessionState::AccessComplete;
		} else {
			status = ALIRO_INVALID_STATE;
		}
		return status;
	};

	if (handle.IsBle()) {
		size_t offset = 0;
		while (offset < data.mLength) {
			/* credential messages may span several L2CAP SDUs. Assemble exactly one
			 * framed message before handing it to the protocol parser. */
			std::array<uint8_t, kBleFrameCapacity> frame{};
			size_t frameLength = 0;
			bool invalidFrame = false;
			{
				StackLock lock;
				SessionContext *session = FindSession(handle);
				if (session == nullptr) {
					return;
				}

				if (session->mBleRxLength < ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE) {
					const size_t headerRemaining =
						ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE - session->mBleRxLength;
					const size_t copied =
						std::min(headerRemaining, data.mLength - offset);
					std::copy_n(data.mData + offset, copied,
						    session->mBleRxBuffer.begin() +
							    session->mBleRxLength);
					session->mBleRxLength += copied;
					offset += copied;
				}
				if (session->mBleRxLength < ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE) {
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
					LOG_INF("ULTRAWIDELOCK_TRACE SOURCE BLE_REASSEMBLY buffered=%zu "
						"header=pending",
						session->mBleRxLength);
#endif
					return;
				}

				const size_t payloadLength =
					(static_cast<size_t>(session->mBleRxBuffer[2]) << 8) |
					session->mBleRxBuffer[3];
				const size_t expectedLength =
					ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE + payloadLength;
				if ((session->mBleRxBuffer[0] & 0xc0u) != 0 || payloadLength == 0 ||
				    expectedLength > session->mBleRxBuffer.size()) {
					invalidFrame = true;
				} else {
					const size_t copied =
						std::min(expectedLength - session->mBleRxLength,
							 data.mLength - offset);
					std::copy_n(data.mData + offset, copied,
						    session->mBleRxBuffer.begin() +
							    session->mBleRxLength);
					session->mBleRxLength += copied;
					offset += copied;
					if (session->mBleRxLength < expectedLength) {
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
						LOG_INF("ULTRAWIDELOCK_TRACE SOURCE BLE_REASSEMBLY "
							"buffered=%zu expected=%zu",
							session->mBleRxLength, expectedLength);
#endif
						return;
					}
					frameLength = expectedLength;
					std::copy_n(session->mBleRxBuffer.begin(), frameLength,
						    frame.begin());
					session->mBleRxLength = 0;
				}
			}
			if (invalidFrame) {
				LOG_WRN("Malformed or oversized credential BLE frame");
				AliroStack::Instance().DestroySession(handle);
				return;
			}

			struct ultrawidelock_cred_ble_message message;
			size_t consumed = 0;
			if (ultrawidelock_cred_ble_parse_message(frame.data(), frameLength, &message,
							&consumed) != ULTRAWIDELOCK_CRED_BLE_OK ||
			    consumed != frameLength) {
				LOG_WRN("Cannot parse assembled credential BLE frame");
				AliroStack::Instance().DestroySession(handle);
				return;
			}
			bool terminate = false;
			bool forwardToUwb = false;
			AliroError status = ALIRO_NO_ERROR;
			std::array<uint8_t, kApduBufferSize + ULTRAWIDELOCK_CRED_BLE_HEADER_SIZE>
				plaintext{};
			size_t plaintextLength = 0;
			{
				StackLock lock;
				SessionContext *session = FindSession(handle);
				if (session == nullptr) {
					return;
				}
				if (session->mState == SessionState::BleConnected) {
					const uint8_t *proprietary = nullptr;
					size_t proprietaryLength = 0;
					ultrawidelock_cred_select_response selected;
					if (ultrawidelock_cred_ble_parse_initiate_access(
						    &message, &proprietary, &proprietaryLength) !=
						    ULTRAWIDELOCK_CRED_BLE_OK ||
					    ultrawidelock_cred_parse_proprietary_information(
						    proprietary, proprietaryLength,
						    ULTRAWIDELOCK_CRED_SELECT_EXPEDITED,
						    &selected) != ULTRAWIDELOCK_CRED_SELECT_OK ||
					    selected.selected_protocol_version !=
						    session->mProtocolVersion ||
					    proprietaryLength >
						    session->mProprietaryInformation.size()) {
						status = ALIRO_INVALID_DATA_FORMAT;
					} else {
						terminate = ObserveResponseTimeoutMessage(
							*session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_INCOMING,
							frame.data(), frameLength);
						session->mProprietaryInformationLength =
							proprietaryLength;
						session->mMaxCommandData = std::min(
							{static_cast<size_t>(
								 selected.max_command_data_length),
							 session->mTxBuffer.size() - 9,
							 kBleMaxCommandData});
						session->mMaxResponseData = std::min(
							{static_cast<size_t>(
								 selected.max_response_data_length),
							 session->mExchangeBuffer.size() - 2,
							 kBleMaxResponseData});
#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
						LOG_INF("ULTRAWIDELOCK_TRACE SOURCE BLE_APDU_LIMITS "
							"command=%zu response=%zu",
							session->mMaxCommandData,
							session->mMaxResponseData);
#endif
						std::copy_n(
							proprietary, proprietaryLength,
							session->mProprietaryInformation.begin());
						status = SendAuth0(*session);
					}
				} else if (session->mState == SessionState::UwbRanging) {
					status = DecryptBleMessage(
						*session, message, plaintext.data(),
						plaintext.size(), plaintextLength);
					if (status == ALIRO_NO_ERROR) {
						enum ultrawidelock_cred_ble_timeout_message timeoutMessage;
						const bool timeoutControl =
							ultrawidelock_cred_ble_timeout_classify(
								plaintext.data(), plaintextLength,
								&timeoutMessage) ==
								ULTRAWIDELOCK_CRED_BLE_OK &&
							(timeoutMessage ==
								 ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_BUSY ||
							 timeoutMessage ==
								 ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR);
						const bool expectedTimeoutControl =
							timeoutControl &&
							session->mResponseTimeout.role ==
								ULTRAWIDELOCK_CRED_BLE_TIMEOUT_LOCAL_TRANSMITTER;
						terminate = ObserveResponseTimeoutMessage(
							*session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_INCOMING,
							plaintext.data(), plaintextLength);
						if (timeoutControl) {
							if (!expectedTimeoutControl) {
								/* Authenticated Busy/General Error
								 * with no outstanding reader
								 * request: the peer is ending
								 * ranging, not corrupting it. Close
								 * cleanly instead of flagging a
								 * fault. */
								LOG_INF("Peer ended ranging via "
									"control notification "
									"(proto=%u id=%u); closing "
									"session",
									static_cast<unsigned int>(
										message.protocol),
									static_cast<unsigned int>(
										message.message_id));
								terminate = true;
							}
						} else {
							forwardToUwb =
								ultrawidelock_cred_ble_is_uwb_control_message(
									&message);
							if (!forwardToUwb) {
								/* Authenticated but non-ranging
								 * message during UWB ranging: the
								 * peer is winding the session down,
								 * not sending bad data. */
								LOG_INF("Non-ranging BLE message "
									"during ranging (proto=%u "
									"id=%u); closing session",
									static_cast<unsigned int>(
										message.protocol),
									static_cast<unsigned int>(
										message.message_id));
								terminate = true;
							}
						}
					}
				} else {
					enum ultrawidelock_cred_ble_timeout_message timeoutMessage;
					const bool timeoutControl =
						ultrawidelock_cred_ble_timeout_classify(
							frame.data(), frameLength,
							&timeoutMessage) == ULTRAWIDELOCK_CRED_BLE_OK &&
						(timeoutMessage ==
							 ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_BUSY ||
						 timeoutMessage ==
							 ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR);
					if (timeoutControl) {
						const bool expected =
							session->mResponseTimeout.role ==
							ULTRAWIDELOCK_CRED_BLE_TIMEOUT_LOCAL_TRANSMITTER;
						terminate = ObserveResponseTimeoutMessage(
							*session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_INCOMING,
							frame.data(), frameLength);
						if (!expected) {
							status = ALIRO_INVALID_DATA_CONTENT;
						}
					} else if (message.protocol != ULTRAWIDELOCK_CRED_BLE_PROTOCOL_AP ||
						   message.message_id != 1) {
						status = ALIRO_INVALID_DATA_CONTENT;
					} else {
						terminate = ObserveResponseTimeoutMessage(
							*session, ULTRAWIDELOCK_CRED_BLE_TIMEOUT_INCOMING,
							frame.data(), frameLength);
						if (!terminate) {
							status = processApResponse(
								*session,
								{const_cast<uint8_t *>(
									 message.payload),
								 message.payload_length},
								terminate);
						}
					}
				}
				if (status != ALIRO_NO_ERROR) {
					LOG_WRN("credential BLE session failed in state %u: %s",
						static_cast<unsigned int>(session->mState),
						status.ToString());
					terminate = true;
				}
			}
			if (forwardToUwb &&
			    Interface::Uwb::HandleBleMessage(handle, plaintext.data(),
							     plaintextLength) != 0) {
				terminate = true;
			}
			if (terminate) {
				AliroStack::Instance().DestroySession(handle);
				return;
			}
		}
		return;
	}

	bool terminate = false;
	AliroError status;
	{
		StackLock lock;
		SessionContext *session = FindSession(handle);
		if (session == nullptr) {
			return;
		}
		status = processApResponse(*session, data, terminate);
		if (status != ALIRO_NO_ERROR) {
			LOG_WRN("credential NFC session failed in state %u: %s",
				static_cast<unsigned int>(session->mState), status.ToString());
			terminate = true;
		}
	}
	if (terminate) {
		AliroStack::Instance().DestroySession(handle);
	}
}

/**
 * Validates the response timeout expiration (session exists, is BLE, timer handle is valid,
 * generation matches, and timeout is not idle), destroys the session on confirmed timeout, and logs
 * a warning.
 */
void ProcessResponseTimeout(size_t sessionIndex, uint32_t generation)
{
	std::optional<ConnectionHandle> expiredHandle;
	{
		StackLock lock;
		if (sessionIndex >= sSessions.size()) {
			return;
		}
		SessionContext &session = sSessions[sessionIndex];
		if (!session.mHandle.has_value() || !session.mHandle->IsBle() ||
		    session.mResponseTimer == Interface::Os::Timer::kInvalidHandle ||
		    session.mResponseTimerGeneration != generation ||
		    session.mResponseTimeout.role == ULTRAWIDELOCK_CRED_BLE_TIMEOUT_IDLE ||
		    Interface::Os::Timer::IsRunning(session.mResponseTimer)) {
			return;
		}
		expiredHandle = session.mHandle;
		session.mResponseTimeout = {};
		NextResponseTimerGeneration(session);
	}

	LOG_WRN("credential BLE response timeout expired");
	AliroStack::Instance().DestroySession(*expiredHandle);
}

} // namespace

/**
 * Accept incoming session data from BLE or NFC and defer or process it synchronously.
 *
 * On NFC, processes immediately. On BLE, enqueues as SessionDataEvent for deferred processing to
 * avoid deadlock. Destroys session on invalid length, allocation failure, or queueing error. Caller
 * must ensure handle is valid; data may be null.
 */
void AliroStack::HandleSessionData(ConnectionHandle handle, Data data)
{
	if (!handle.IsBle()) {
		ProcessSessionData(handle, data);
		return;
	}
	if (data.mData == nullptr || data.mLength == 0 ||
	    data.mLength > kSessionDataEventCapacity) {
		LOG_WRN("Cannot defer credential BLE data: invalid length %zu", data.mLength);
		DestroySession(handle);
		return;
	}

	auto *event = new (std::nothrow) SessionDataEvent(handle, data);
	if (event == nullptr) {
		LOG_WRN("Cannot defer credential BLE data: no memory");
		DestroySession(handle);
		return;
	}
	const AliroError status = Interface::Os::QueueEvent(event);
	if (status != ALIRO_NO_ERROR) {
		LOG_WRN("Cannot defer credential BLE data: %s", status.ToString());
		delete event;
		DestroySession(handle);
	}
}

#ifdef CONFIG_NCS_ALIRO_BLE_UWB

/**
 * Finds the session in UwbRanging state, encrypts the message using the BLE reader key, logs a
 * warning and destroys the session on failure.
 */
void AliroStack::SendBleMessage(ConnectionHandle connectionHandle, const uint8_t *data,
				size_t length) const
{
	AliroError status = ALIRO_SESSION_NOT_FOUND;
	{
		StackLock lock;
		SessionContext *session = FindSession(connectionHandle);
		if (session != nullptr && session->mState == SessionState::UwbRanging) {
			status = EncryptBleMessage(*session, data, length);
		}
	}
	if (status != ALIRO_NO_ERROR) {
		LOG_WRN("Failed to send credential BLE message: %s", status.ToString());
		if (status != ALIRO_SESSION_NOT_FOUND) {
			const_cast<AliroStack *>(this)->DestroySession(connectionHandle);
		}
	}
}

/**
 * Encrypt and broadcast a Reader Status Changed message to all BLE sessions in UWB ranging state,
 * optionally filtering by credential public key.
 *
 * Returns the first error encountered, or ALIRO_NO_ERROR if all sessions received the message (or
 * were not BLE/not ranging). Used to signal reader state changes (e.g., unlocked, secured) during
 * active UWB sessions.
 */
AliroError AliroStack::SendReaderStatusChangedMessage(
	OperationSource operationSource, ReaderStateByte readerState,
	const CryptoTypes::PublicKey *accessCredentialPublicKey) const
{
	uint8_t plaintext[8];
	if (ultrawidelock_cred_ble_build_reader_status_changed(static_cast<uint8_t>(operationSource),
						      static_cast<uint8_t>(readerState),
						      plaintext) != ULTRAWIDELOCK_CRED_BLE_OK) {
		return ALIRO_ERROR_INTERNAL;
	}
	AliroError result = ALIRO_NO_ERROR;
	StackLock lock;
	for (auto &session : sSessions) {
		if (!session.mHandle.has_value() || !session.mHandle->IsBle() ||
		    session.mState != SessionState::UwbRanging ||
		    (accessCredentialPublicKey != nullptr &&
		     session.mCredentialPublicKey != *accessCredentialPublicKey)) {
			continue;
		}
		const AliroError status = EncryptBleMessage(session, plaintext, sizeof(plaintext));
		if (status != ALIRO_NO_ERROR && result == ALIRO_NO_ERROR) {
			result = status;
		}
	}
	return result;
}

#endif // CONFIG_NCS_ALIRO_BLE_UWB

/**
 * Dequeue and process a pending credential event (ResponseTimeoutEvent or SessionDataEvent) or log
 * and ignore unknown events.
 *
 * Deletes the event after processing. Called by the OS event loop when an credential-owned event is
 * ready. Caller must pass a non-null event pointer; null triggers a warning log.
 */
void AliroStack::ProcessEvent(void *event)
{
	if (event == nullptr) {
		LOG_WRN("Cannot process null credential event");
		return;
	}
	auto *header = static_cast<EventHeader *>(event);
	if (header->mMagic == kResponseTimeoutEventMagic) {
		auto *timeout = static_cast<ResponseTimeoutEvent *>(event);
		const size_t sessionIndex = timeout->mSessionIndex;
		const uint32_t generation = timeout->mGeneration;
		timeout->mMagic = 0;
		delete timeout;
		ProcessResponseTimeout(sessionIndex, generation);
		return;
	}
	if (header->mMagic != kSessionDataEventMagic) {
		LOG_WRN("Cannot process unknown credential event");
		return;
	}
	auto *sessionData = static_cast<SessionDataEvent *>(event);

#ifdef CONFIG_ULTRAWIDELOCK_CRED_TRACE
	LOG_INF("ULTRAWIDELOCK_TRACE SOURCE DEFERRED_RX len=%zu", sessionData->mLength);
#endif
	ProcessSessionData(sessionData->mHandle, {sessionData->mData.data(), sessionData->mLength});
	sessionData->mMagic = 0;
	delete sessionData;
}

} // namespace Aliro
