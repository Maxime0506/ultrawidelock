/* stackfake: aliro/interface.h — every callback the stack makes into the
 * application and the platform.
 *
 * Signatures are transcribed from upstream; the bodies are in stackfake.cpp.
 * NOTHING HERE DOES CRYPTOGRAPHY. Key agreement, HKDF, AES-GCM, ECDSA and
 * SHA-256 are all deterministic stand-ins, so a suite built on this can show
 * which branch the state machine takes and which arguments it passed, and can
 * never show that a ciphertext, a signature or a derived key is correct. */
#ifndef STACKFAKE_ALIRO_INTERFACE_H
#define STACKFAKE_ALIRO_INTERFACE_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include <aliro/connection_handle.h>
#include <aliro/errors.h>
#include <aliro/protocol_version.h>
#include <aliro/time.h>
#include <aliro/types.h>

namespace Aliro::Interface
{

namespace Access
{

/** What the application asks the User Device for during step-up. */
struct AccessDocumentRequestParams {
	ConstData mElementIdentifier;
	bool mIntentToStore;
};

std::optional<AccessDocumentRequestParams>
GetAccessDocumentRequestParameters(const CryptoTypes::PublicKey &publicKey,
				   const std::optional<Timestamp> &credentialSignedTimestamp);

AliroError ProcessAccessRequest(
	ConnectionHandle handle, const CryptoTypes::PublicKey &userPublicKey,
	CryptoTypes::KeyId kpersistentKeyId,
	const std::optional<AccessDocumentTypes::AccessDocument> &accessDocument = std::nullopt);

AliroError ProcessAccessRequest(ConnectionHandle handle, CryptoTypes::KeyId kpersistentKeyId);

AliroError GetCredentialIssuerPublicKey(const CryptoTypes::KeyIdentifier &keyIdentifier,
					CryptoTypes::PublicKey &publicKey);

AliroError GetKpersistentCount(size_t &count);
AliroError GetKpersistentKeyIds(CryptoTypes::KeyId *keyIds, size_t &count);
AliroError GetAccessCredentialPublicKey(CryptoTypes::KeyId kpersistentKeyId,
					CryptoTypes::PublicKey &publicKey);

} // namespace Access

namespace Reader
{

AliroError GetIdentifier(Identifier &identifier);
AliroError GetPublicKey(CryptoTypes::PublicKey &publicKey);
bool IsCertificateProvisioned();
AliroError GetIssuerPublicKey(CryptoTypes::PublicKey &publicKey);
AliroError GetCertificate(ConstData &certificate);

} // namespace Reader

namespace Ble
{

size_t GetMaxSessions();
ProtocolVersion GetProtocolVersion(ConnectionHandle handle);

} // namespace Ble

namespace Uwb
{

int HandleBleMessage(ConnectionHandle connectionHandle, const uint8_t *data, size_t length);

} // namespace Uwb

namespace Session
{

AliroError Send(ConnectionHandle handle, Data data);
void HandleTermination(ConnectionHandle handle);
AliroError StartRangingSession(ConnectionHandle handle, uint32_t rangingSessionId,
			       const CryptoTypes::Ursk &ursk, ProtocolVersion protocolVersion);

} // namespace Session

namespace CredentialIssuerCertificate
{

/** Validity window carried by an issuer certificate. */
struct CertificateTimestamps {
	Time mValidFrom;
	Time mValidUntil;
};

AliroError Validate(const ConstData &certificate, CryptoTypes::PublicKey &publicKey,
		    std::optional<CertificateTimestamps> &timestamps);

} // namespace CredentialIssuerCertificate

namespace AccessDocument
{

/** std::nullopt when the reader has no trusted time to judge against. */
std::optional<bool> VerifyValidityPeriod(const Time &validFrom, const Time &validUntil);

} // namespace AccessDocument

namespace Os
{

AliroError QueueEvent(void *event);

namespace Mutex
{
void Lock();
void Unlock();
} // namespace Mutex

namespace Timer
{
using Handle = int;
constexpr Handle kInvalidHandle{-1};
using Callback = void (*)(void *context);

Handle Acquire(Callback callback, void *context);
void Release(Handle handle);
void Start(Handle handle, uint32_t timeoutMs);
void Stop(Handle handle);
bool IsRunning(Handle handle);
} // namespace Timer

} // namespace Os

namespace Crypto
{

AliroError GenerateRandom(uint8_t *buffer, size_t bufferLength);
AliroError GenerateEphemeralKeyPair(CryptoTypes::KeyId &keyId,
				    CryptoTypes::PublicKey &ephemeralPubKey);
AliroError ImportSharedKey(const uint8_t *key, size_t keyLength, CryptoTypes::KeyId &keyId);
AliroError ImportSymmetricKey(const uint8_t *key, size_t keyLength, CryptoTypes::KeyId &keyId);
AliroError DestroyKey(CryptoTypes::KeyId &keyId);
AliroError GenerateSignature(const uint8_t *msg, const size_t msgLength,
			     CryptoTypes::Signature &signature);
AliroError VerifySignature(const CryptoTypes::PublicKey &publicKey, const uint8_t *msg,
			   const size_t msgLength, const CryptoTypes::Signature &signature);
AliroError RawKeyAgreement(CryptoTypes::KeyId keyId, const CryptoTypes::PublicKey &peerPublicKey,
			   CryptoTypes::SharedSecret &sharedSecret);
AliroError DeriveSharedKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			   const uint8_t *salt, size_t saltLength, CryptoTypes::KeyId &derivedKeyId);
AliroError DeriveSymmetricKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			      const uint8_t *salt, size_t saltLength,
			      CryptoTypes::KeyId &derivedKeyId);
AliroError DeriveRawKey(CryptoTypes::KeyId keyId, const uint8_t *info, size_t infoLength,
			const uint8_t *salt, size_t saltLength, uint8_t *derivedKey,
			size_t derivedKeyLength);
AliroError AeadEncrypt(CryptoTypes::KeyId keyId, const uint8_t *plainTxt, size_t plainTxtLength,
		       const uint8_t *additionalData, size_t additionalDataLength,
		       const CryptoTypes::Nonce &nonce, uint8_t *cipherText,
		       CryptoTypes::AuthenticationTag &authTag);
AliroError AeadDecrypt(CryptoTypes::KeyId keyId, const uint8_t *cipherTextWithTag,
		       size_t cipherTextWithTagLength, const uint8_t *additionalData,
		       size_t additionalDataLength, const CryptoTypes::Nonce &nonce,
		       uint8_t *plainText, size_t &plainTextLength);
AliroError Encrypt(const uint8_t *plainText, size_t plainTextLength, uint8_t *cipherText);
AliroError Sha256(const uint8_t *data, size_t dataLength, CryptoTypes::Sha256Hash &hash);

} // namespace Crypto

} // namespace Aliro::Interface

#endif /* STACKFAKE_ALIRO_INTERFACE_H */
