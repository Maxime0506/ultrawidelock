/* stackfake: aliro/aliro.h — the stack's own public surface.
 *
 * Every method here is DEFINED by the code under test (aliro_stack.cpp and
 * session.cpp); only Instance() lives in the header, exactly as upstream. */
#ifndef STACKFAKE_ALIRO_ALIRO_H
#define STACKFAKE_ALIRO_ALIRO_H

#include <cstddef>
#include <cstdint>

#include <aliro/ble_types.h>
#include <aliro/connection_handle.h>
#include <aliro/errors.h>
#include <aliro/protocol_version.h>
#include <aliro/types.h>

namespace Aliro
{

static constexpr uint8_t kFeatureExpeditedFastPhaseSupported = static_cast<uint8_t>(1u << 0);
static constexpr uint8_t kFeatureStepUpPhaseSupported = static_cast<uint8_t>(1u << 1);
static constexpr uint8_t kFeatureBleUwbSupported = static_cast<uint8_t>(1u << 2);

/** The Aliro reader stack. One instance, reached through Instance(). */
class AliroStack
{
      public:
	static AliroStack &Instance()
	{
		static AliroStack sInstance;

		return sInstance;
	}

	AliroError Init();
	static const char *GetLibraryVersion();
	const ProtocolVersion *GetExpeditedStandardProtocolVersions(size_t &versionCount) const;
	uint8_t GetFeatures() const;

	AliroError CreateSession(ConnectionHandle connectionHandle);
	void DestroySession(ConnectionHandle connectionHandle);
	void HandleSessionData(ConnectionHandle handle, Data data);
	void ProcessEvent(void *event);

#ifdef CONFIG_NCS_ALIRO_BLE_UWB
	static AliroError
	GenerateAdvertisingData(BleTypes::AdvertisingServiceData &outData,
				const BleTypes::BleAddress &address,
				BleTypes::TxPowerLevel txPowerLevel,
				const Identifier &readerIdentifier,
				BleTypes::AdvertisingServiceData::Notification notification,
				BleTypes::BleExpiryTimestamp expirationTime);
	static uint8_t GetBleAdvertisingVersion();
	const ProtocolVersion *GetBleUwbProtocolVersions(size_t &versionCount) const;
	void SendBleMessage(ConnectionHandle connectionHandle, const uint8_t *data,
			    size_t length) const;
	AliroError SendReaderStatusChangedMessage(
		OperationSource operationSource, ReaderStateByte readerState,
		const CryptoTypes::PublicKey *accessCredentialPublicKey = nullptr) const;
#endif
};

} // namespace Aliro

#endif /* STACKFAKE_ALIRO_ALIRO_H */
