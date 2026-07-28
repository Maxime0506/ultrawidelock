<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/aliro_stack.cpp`

Independent implementation of the Nordic Aliro public API used by this app.
Protocol constants and wire formats come from Aliro Specification 1.0.

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](advertising_core.h.md)

## API

### `bool IsDigit(uint8_t value)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:64`

Returns true if value is an ASCII digit character (0x30–0x39).

**called by** `ParseDecimal`

### `int ParseDecimal(const uint8_t *value, size_t length)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:73`

Parses a decimal number from length bytes starting at value. Returns the parsed integer, or –1 if
any byte is not an ASCII digit.

**called by** `FromTimestamp`  ·  **calls** `IsDigit`

### `bool IsLeapYear(int year)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:88`

Returns true if year is a leap year.

**called by** `IsValidDate`

### `bool IsValidDate(int year, int month, int day)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:97`

Returns true if the given year, month, and day form a valid date. Accounts for leap years;
rejects dates with year ≤ 0, month outside [1, 12], or day outside [1, days-in-month].

**called by** `FromTimestamp`  ·  **calls** `IsLeapYear`

### `const char *AliroError::ToString() const`
`modules/woz_aliro_stack/src/aliro_stack.cpp:116`

Return a human-readable null-terminated string for this error code. Valid for all error enum
values; returns "Unknown error" if the code is out of bounds.

### `AliroError AliroError::FromInt(int ec)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:127`

Convert an integer error code to an AliroError object. Returns ALIRO_ERROR_UNKNOWN if the code is
out of bounds (negative or >= ALIRO_ERROR_MAX); otherwise returns the corresponding error enum
value.

### `std::optional<Time> Time::FromTimestamp(const uint8_t *timestamp, size_t length)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:147`

Parses an RFC 3339 fixed-width timestamp in the form YYYY-MM-DDTHH:MM:SSZ (exactly 20 bytes).
Returns a Time object on success; returns std::nullopt if the input is null, the wrong length, or
contains an invalid date or time component.

**calls** `IsValidDate`, `ParseDecimal`

### `void BleTypes::AdvertisingServiceData::SetVersion(uint8_t version)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:172`

Sets the advertising version field (masked to kAdvertisingVersionMask bits).

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetNotification(Notification notification)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:180`

Sets the notification field (masked to kNotificationMask bits).

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetTxPowerLevel(TxPowerLevel powerLevelDbm)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:189`

Sets the TX power level in dBm.

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetTruncatedReaderGroupId(const uint8_t *readerGroupId)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:198`

Copies the first kTruncatedReaderGroupIdLength bytes of readerGroupId into
mTruncatedReaderGroupId.

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetTruncatedReaderGroupSubId(const uint8_t *readerGroupSubId)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:207`

Copies the first kTruncatedReaderGroupSubIdLength bytes of readerGroupSubId into
mTruncatedReaderGroupSubId.

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetDynamicTagExpiryTimestamp(BleExpiryTimestamp expiryTimestampUnix)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:216`

Encodes expiryTimestampUnix as a big-endian 32-bit integer into mDynamicTagExpiryTime.

**called by** `GenerateAdvertisingData`

### `void BleTypes::AdvertisingServiceData::SetDynamicTag(const uint8_t *dynamicTag)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:228`

Copies dynamicTag (kWOZ_ALIRO_DYNAMIC_TAG_SIZE bytes) into mDynamicTag.

**called by** `GenerateAdvertisingData`

### `AliroError AliroStack::Init()`
`modules/woz_aliro_stack/src/aliro_stack.cpp:236`

Initializes the Aliro stack. Returns ALIRO_NO_ERROR on success.

### `const char *AliroStack::GetLibraryVersion()`
`modules/woz_aliro_stack/src/aliro_stack.cpp:245`

Returns the library version string.

### `const ProtocolVersion *AliroStack::GetExpeditedStandardProtocolVersions(size_t &versionCount) const`
`modules/woz_aliro_stack/src/aliro_stack.cpp:254`

Returns the array of protocol versions supported for expedited standard procedure, and sets
versionCount to the array length.

### `uint8_t AliroStack::GetFeatures() const`
`modules/woz_aliro_stack/src/aliro_stack.cpp:264`

Returns a bitmask of supported features: expedited fast phase, step-up phase, and BLE UWB, based
on build configuration.

### `AliroError AliroStack::GenerateAdvertisingData(BleTypes::AdvertisingServiceData &outData, const BleTypes::BleAddress &address, BleTypes::TxPowerLevel txPowerLevel, const Identifier &readerIdentifier, BleTypes::AdvertisingServiceData::Notification notification, BleTypes::BleExpiryTimestamp expirationTime)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:287`

Generates an Aliro BLE advertisement from the provided address, power level, reader identifier,
notification state, and expiration timestamp. Validates inputs and derives the dynamic tag via
encryption. Returns ALIRO_NO_ERROR on success; ALIRO_INVALID_ARGUMENT if txPowerLevel is outside
[–100, 20] dBm or notification is out of range.

**calls** `GetBleAdvertisingVersion`, `SetDynamicTag`, `SetDynamicTagExpiryTimestamp`, `SetNotification`, `SetTruncatedReaderGroupId`, `SetTruncatedReaderGroupSubId`, `SetTxPowerLevel`, `SetVersion`

### `uint8_t AliroStack::GetBleAdvertisingVersion()`
`modules/woz_aliro_stack/src/aliro_stack.cpp:327`

Returns the BLE advertising version supported by this stack.

**called by** `GenerateAdvertisingData`

### `const ProtocolVersion *AliroStack::GetBleUwbProtocolVersions(size_t &versionCount) const`
`modules/woz_aliro_stack/src/aliro_stack.cpp:336`

Returns the array of protocol versions supported over BLE UWB, and sets versionCount to the array
length.
