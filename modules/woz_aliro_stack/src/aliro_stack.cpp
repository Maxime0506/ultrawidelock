/*
 * Clean-room implementation of the Nordic Aliro public API used by this app.
 * Protocol constants and wire formats come from Aliro Specification 1.0.
 * No implementation detail from the proprietary archive is used here.
 */

#include "advertising_core.h"

#include <aliro/aliro.h>
#include <aliro/interface.h>
#include <aliro/time.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

LOG_MODULE_REGISTER(woz_aliro_source, CONFIG_NCS_ALIRO_LOG_LEVEL_VALUE);

namespace
{

constexpr Aliro::ProtocolVersion kProtocolVersions[]{0x0100};

constexpr const char *kErrorStrings[]{
	"No error",
	"No memory",
	"Internal error",
	"Invalid state",
	"Invalid argument",
	"Invalid signature",
	"Invalid authentication tag",
	"Public key not found",
	"Public key expired",
	"Public key not trusted",
	"Key already exists",
	"Timeout",
	"Not implemented",
	"TLV invalid tag",
	"TLV invalid length",
	"TLV buffer too small",
	"TLV wrong data type",
	"End of TLV",
	"Unknown error",
	"Session not found",
	"Terminate session",
	"Version not supported",
	"Invalid APDU status",
	"Encryption counter overflow",
	"Decryption counter overflow",
	"Invalid data format",
	"Invalid data content",
	"No public key in response",
};

static_assert(std::size(kErrorStrings) == ALIRO_ERROR_MAX);

bool IsDigit(uint8_t value)
{
	return value >= '0' && value <= '9';
}

int ParseDecimal(const uint8_t *value, size_t length)
{
	int result = 0;
	for (size_t i = 0; i < length; ++i) {
		if (!IsDigit(value[i])) {
			return -1;
		}
		result = result * 10 + (value[i] - '0');
	}
	return result;
}

bool IsLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool IsValidDate(int year, int month, int day)
{
	constexpr int kDaysPerMonth[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (year <= 0 || month < 1 || month > 12 || day < 1) {
		return false;
	}
	int days = kDaysPerMonth[month - 1];
	if (month == 2 && IsLeapYear(year)) {
		++days;
	}
	return day <= days;
}

} // namespace

const char *AliroError::ToString() const
{
	const unsigned int code = static_cast<unsigned int>(mCode);
	return code < std::size(kErrorStrings) ? kErrorStrings[code] : "Unknown error";
}

AliroError AliroError::FromInt(int ec)
{
	if (ec < 0 || ec >= ALIRO_ERROR_MAX) {
		return ALIRO_ERROR_UNKNOWN;
	}
	return static_cast<AliroErrorCode>(ec);
}

namespace Aliro
{

static_assert(sizeof(BleTypes::AdvertisingServiceData::ServiceFlags) == 1);
static_assert(sizeof(BleTypes::AdvertisingServiceData) == 24);
static_assert(sizeof(BleTypes::AdvertisingService) == 26);

std::optional<Time> Time::FromTimestamp(const uint8_t *timestamp, size_t length)
{
	/* Timestamp is the fixed-width RFC 3339 form YYYY-MM-DDTHH:MM:SSZ. */
	if (timestamp == nullptr || length != kTimestampLength || timestamp[4] != '-' ||
	    timestamp[7] != '-' || timestamp[10] != 'T' || timestamp[13] != ':' ||
	    timestamp[16] != ':' || timestamp[19] != 'Z') {
		return std::nullopt;
	}

	const int year = ParseDecimal(timestamp, 4);
	const int month = ParseDecimal(timestamp + 5, 2);
	const int day = ParseDecimal(timestamp + 8, 2);
	const int hour = ParseDecimal(timestamp + 11, 2);
	const int minute = ParseDecimal(timestamp + 14, 2);
	const int second = ParseDecimal(timestamp + 17, 2);
	if (!IsValidDate(year, month, day) || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
	    second < 0 || second > 60) {
		return std::nullopt;
	}
	return Time(year, month, day, hour, minute, second);
}

void BleTypes::AdvertisingServiceData::SetVersion(uint8_t version)
{
	mServiceFlags.version = version & static_cast<uint8_t>(kAdvertisingVersionMask);
}

void BleTypes::AdvertisingServiceData::SetNotification(Notification notification)
{
	mServiceFlags.notification =
		static_cast<uint8_t>(notification) & static_cast<uint8_t>(kNotificationMask);
}

void BleTypes::AdvertisingServiceData::SetTxPowerLevel(TxPowerLevel powerLevelDbm)
{
	mTxPowerLevelDbm = powerLevelDbm;
}

void BleTypes::AdvertisingServiceData::SetTruncatedReaderGroupId(const uint8_t *readerGroupId)
{
	std::memcpy(mTruncatedReaderGroupId, readerGroupId, sizeof(mTruncatedReaderGroupId));
}

void BleTypes::AdvertisingServiceData::SetTruncatedReaderGroupSubId(const uint8_t *readerGroupSubId)
{
	std::memcpy(mTruncatedReaderGroupSubId, readerGroupSubId,
		    sizeof(mTruncatedReaderGroupSubId));
}

void BleTypes::AdvertisingServiceData::SetDynamicTagExpiryTimestamp(
	BleExpiryTimestamp expiryTimestampUnix)
{
	mDynamicTagExpiryTime[0] = static_cast<uint8_t>(expiryTimestampUnix >> 24);
	mDynamicTagExpiryTime[1] = static_cast<uint8_t>(expiryTimestampUnix >> 16);
	mDynamicTagExpiryTime[2] = static_cast<uint8_t>(expiryTimestampUnix >> 8);
	mDynamicTagExpiryTime[3] = static_cast<uint8_t>(expiryTimestampUnix);
}

void BleTypes::AdvertisingServiceData::SetDynamicTag(const uint8_t *dynamicTag)
{
	std::memcpy(mDynamicTag, dynamicTag, sizeof(mDynamicTag));
}

AliroError AliroStack::Init()
{
	LOG_INF("Clean-room Aliro source stack enabled");
	return ALIRO_NO_ERROR;
}

const char *AliroStack::GetLibraryVersion()
{
	return "openaliro-cleanroom/0.2";
}

const ProtocolVersion *AliroStack::GetExpeditedStandardProtocolVersions(size_t &versionCount) const
{
	versionCount = std::size(kProtocolVersions);
	return kProtocolVersions;
}

uint8_t AliroStack::GetFeatures() const
{
	uint8_t features = 0;
#if defined(CONFIG_DOOR_LOCK_EXPEDITED_FAST_PHASE)
	features |= kFeatureExpeditedFastPhaseSupported;
#endif
#if defined(CONFIG_DOOR_LOCK_STEP_UP_PHASE)
	features |= kFeatureStepUpPhaseSupported;
#endif
#if defined(CONFIG_NCS_ALIRO_BLE_UWB)
	features |= kFeatureBleUwbSupported;
#endif
	return features;
}

#ifdef CONFIG_NCS_ALIRO_BLE_UWB

AliroError AliroStack::GenerateAdvertisingData(
	BleTypes::AdvertisingServiceData &outData, const BleTypes::BleAddress &address,
	BleTypes::TxPowerLevel txPowerLevel, const Identifier &readerIdentifier,
	BleTypes::AdvertisingServiceData::Notification notification,
	BleTypes::BleExpiryTimestamp expirationTime)
{
	if (txPowerLevel < -100 || txPowerLevel > 20 ||
	    static_cast<uint8_t>(notification) >
		    static_cast<uint8_t>(
			    BleTypes::AdvertisingServiceData::Notification::SensorTriggered)) {
		return ALIRO_INVALID_ARGUMENT;
	}

	outData = BleTypes::AdvertisingServiceData{};
	outData.mServiceFlags.bleUwb = 1;
	outData.SetVersion(GetBleAdvertisingVersion());
	outData.SetNotification(notification);
	outData.SetTxPowerLevel(txPowerLevel);
	outData.SetTruncatedReaderGroupId(readerIdentifier.data());
	outData.SetTruncatedReaderGroupSubId(readerIdentifier.data() +
					     kReaderGroupIdentifierLength);
	outData.SetDynamicTagExpiryTimestamp(expirationTime);

	std::array<uint8_t, WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE> plaintext{};
	std::array<uint8_t, WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE> ciphertext{};
	std::array<uint8_t, WOZ_ALIRO_DYNAMIC_TAG_SIZE> dynamicTag{};
	woz_aliro_dynamic_tag_input(address.data(), expirationTime, plaintext.data());
	AliroError error =
		Interface::Crypto::Encrypt(plaintext.data(), plaintext.size(), ciphertext.data());
	if (error != ALIRO_NO_ERROR) {
		return error;
	}
	woz_aliro_dynamic_tag_from_ciphertext(ciphertext.data(), dynamicTag.data());
	outData.SetDynamicTag(dynamicTag.data());
	return ALIRO_NO_ERROR;
}

uint8_t AliroStack::GetBleAdvertisingVersion()
{
	return 0;
}

const ProtocolVersion *AliroStack::GetBleUwbProtocolVersions(size_t &versionCount) const
{
	versionCount = std::size(kProtocolVersions);
	return kProtocolVersions;
}

#endif // CONFIG_NCS_ALIRO_BLE_UWB

} // namespace Aliro
