/* stackfake: aliro/ble_types.h.
 *
 * THE LAYOUT IS THE POINT. aliro_stack.cpp static_asserts that ServiceFlags is
 * one byte, AdvertisingServiceData is 24 and AdvertisingService is 26 -- those
 * are wire sizes an iPhone parses, and they are checked at compile time
 * precisely so a field added here cannot silently grow the advertisement. So
 * the bitfield order, the packing and every array size are transcribed from
 * upstream, and the asserts in the code under test are doing real work in this
 * build too.
 *
 * The Set* methods are declared only: they are the code under test. */
#ifndef STACKFAKE_ALIRO_BLE_TYPES_H
#define STACKFAKE_ALIRO_BLE_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>

#include <aliro/protocol_version.h>

namespace Aliro::BleTypes
{

constexpr ProtocolVersion kInvalidProtocolVersion{0x0000};
constexpr size_t kBleAddressSize{6};
using BleAddress = std::array<uint8_t, kBleAddressSize>;
constexpr uint16_t kAliroServiceUuid = 0xFFF2;
using BleExpiryTimestamp = uint32_t;
constexpr BleExpiryTimestamp kExpiryTimeUnavailable{0xFFFFFFFF};
using TxPowerLevel = int8_t;

/** The 24-byte Aliro service payload of ADV_IND. */
struct AdvertisingServiceData {
	constexpr static std::byte kAdvertisingVersionMask{0x07};
	constexpr static std::byte kNotificationMask{0x03};
	constexpr static uint8_t kMaxReaderGroupIdSize{8};
	constexpr static uint8_t kMaxReaderGroupSubIdSize{2};
	constexpr static uint8_t kMaxDynamicTagSize{7};

	enum class Notification : uint8_t {
		NoError = 0,
		UnknownError,
		LowBattery,
		SensorTriggered,
	};

	struct ServiceFlags {
		uint8_t version : 3;      /* bits [2:0] advertisement version */
		uint8_t notification : 2; /* bits [4:3] notification */
		uint8_t : 1;              /* bit 5 RFU */
		uint8_t : 1;              /* bit 6 BLE-only flow unsupported */
		uint8_t bleUwb : 1;       /* bit 7 BLE + UWB flow supported */
	} __attribute__((packed));

	ServiceFlags mServiceFlags{.bleUwb = 1};
	TxPowerLevel mTxPowerLevelDbm{};
	uint8_t mTruncatedReaderGroupId[kMaxReaderGroupIdSize]{};
	uint8_t mTruncatedReaderGroupSubId[kMaxReaderGroupSubIdSize]{};
	uint8_t mDynamicTagExpiryTime[sizeof(BleExpiryTimestamp)]{};
	uint8_t mRfu{0};
	uint8_t mDynamicTag[kMaxDynamicTagSize]{};

	/* All six are defined by aliro_stack.cpp -- the code under test. */
	void SetVersion(uint8_t version);
	void SetNotification(Notification notification);
	void SetTxPowerLevel(TxPowerLevel powerLevelDbm);
	void SetTruncatedReaderGroupId(const uint8_t *readerGroupId);
	void SetTruncatedReaderGroupSubId(const uint8_t *readerGroupSubId);
	void SetDynamicTagExpiryTimestamp(BleExpiryTimestamp expiryTimestampUnix);
	void SetDynamicTag(const uint8_t *dynamicTag);
} __attribute__((packed));

/** The service UUID followed by that payload: 26 bytes on the wire. */
struct AdvertisingService {
	uint8_t Uuid[2] = {static_cast<uint8_t>(kAliroServiceUuid & 0xff),
			   static_cast<uint8_t>(kAliroServiceUuid >> 8)};
	AdvertisingServiceData mAdvertisingServiceData{};
} __attribute__((packed));

} // namespace Aliro::BleTypes

#endif /* STACKFAKE_ALIRO_BLE_TYPES_H */
