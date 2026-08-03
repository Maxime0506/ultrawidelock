/* stackfake: aliro/types.h.
 *
 * EVERY LENGTH HERE IS THE UPSTREAM ONE. session.cpp slices buffers by these
 * constants -- the salt layout, the 160-byte derived-key block, the URSK copy
 * offset -- so a fake that rounded any of them off would exercise a different
 * program. Transcribed rather than invented. */
#ifndef STACKFAKE_ALIRO_TYPES_H
#define STACKFAKE_ALIRO_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Aliro
{

constexpr size_t kReaderGroupIdentifierLength{16};
constexpr size_t kReaderGroupSubIdentifierLength{16};
constexpr size_t kReaderIdentifierLength{kReaderGroupIdentifierLength +
					 kReaderGroupSubIdentifierLength};

/** The 32-byte Reader Identifier: group id followed by group sub-id. */
using Identifier = std::array<uint8_t, kReaderIdentifierLength>;

/** A mutable counted buffer. */
struct Data {
	uint8_t *mData;
	size_t mLength;
};

/** A read-only counted buffer. */
struct ConstData {
	const uint8_t *mData;
	size_t mLength;
};

using UwbRangingData = ConstData;

enum class RangingSessionState : uint8_t {
	Idle = 0,
	Active,
	Stopped,
};

enum class ReaderStateByte : uint8_t {
	Secured = 0x00,
	Unsecured = 0x01,
	Blocked = 0x02,
	EnteringSecured = 0x80,
	EnteringUnsecured = 0x81,
	Unknown = 0x82,
};

enum class OperationSource : uint8_t {
	Unspecified = 0x00,
	Manual,
	Auto,
	Schedule,
	ThisUserDeviceInBluetoothLeUwbAliroFlow,
	ThisUserDeviceInNfc,
	ThisUserDeviceInBluetoothLeOnlyFlow,
	Matter,
};

using ValidityIteration = uint64_t;

/** RFC 3339 fixed width: YYYY-MM-DDTHH:MM:SSZ. */
constexpr size_t kTimestampLength{20};
using Timestamp = std::array<uint8_t, kTimestampLength>;

} // namespace Aliro

namespace Aliro::CryptoTypes
{

using KeyId = uint32_t;

constexpr size_t kEccP256KeyPrivateKeyLength{32};
constexpr size_t kReaderSigningKeyLength{kEccP256KeyPrivateKeyLength};
constexpr uint8_t kEccP256PublicKeyPrefix{0x04};
constexpr size_t kEccP256PublicKeyPrefixLength{1};
constexpr size_t kEccP256KeySingleCoordinateLength{32};
constexpr size_t kEccP256SignatureLength{64};
constexpr size_t kEccP256PublicKeyLength{kEccP256PublicKeyPrefixLength +
					 (2 * kEccP256KeySingleCoordinateLength)};
constexpr size_t kSymmetricKeyLength{32};
constexpr size_t kEncryptionDecryptionCounterLength{4};
constexpr size_t kNonceLength{sizeof(uint64_t) + kEncryptionDecryptionCounterLength};
constexpr size_t kTransactionIdentifierLength{16};
constexpr size_t kAuthenticationTagLength{16};
constexpr size_t kGroupResolvingKeyLength{16};
constexpr size_t kKeyIdentifierLength{8};
constexpr size_t kSha256HashLength{32};

using PublicKeyXcoordinate = std::array<uint8_t, kEccP256KeySingleCoordinateLength>;
using PublicKey = std::array<uint8_t, kEccP256PublicKeyLength>;
using PrivateKey = std::array<uint8_t, kEccP256KeyPrivateKeyLength>;
using Ursk = std::array<uint8_t, kSymmetricKeyLength>;
using Signature = std::array<uint8_t, kEccP256SignatureLength>;
using Nonce = std::array<uint8_t, kNonceLength>;
using TransactionIdentifier = std::array<uint8_t, kTransactionIdentifierLength>;
using AuthenticationTag = std::array<uint8_t, kAuthenticationTagLength>;
using GroupResolvingKey = std::array<uint8_t, kGroupResolvingKeyLength>;
using KeyIdentifier = std::array<uint8_t, kKeyIdentifierLength>;
using Sha256Hash = std::array<uint8_t, kSha256HashLength>;
using SharedSecret = std::array<uint8_t, kEccP256KeySingleCoordinateLength>;

} // namespace Aliro::CryptoTypes

namespace Aliro::AccessDocumentTypes
{

enum class DocumentType {
	Access,
	Revocation,
};

/** What the stack hands the application after a validated Access Document. */
struct AccessDocument {
	const CryptoTypes::PublicKey &mPublicKey;
	ConstData mDataElement;
	const CryptoTypes::PublicKey &mCredentialIssuerPublicKey;
	const Timestamp &mSignedTimestamp;
	std::optional<uint64_t> mValidityIteration;
};

} // namespace Aliro::AccessDocumentTypes

#endif /* STACKFAKE_ALIRO_TYPES_H */
