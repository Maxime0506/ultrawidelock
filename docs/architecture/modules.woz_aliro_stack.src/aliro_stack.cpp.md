<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/aliro_stack.cpp`

Independent implementation of the Nordic Aliro public API used by this app.
Protocol constants and wire formats come from Aliro Specification 1.0.

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](advertising_core.h.md)

## API

### `const char *AliroError::ToString() const`
`modules/woz_aliro_stack/src/aliro_stack.cpp:102`

Return a human-readable null-terminated string for this error code. Valid for all error enum
values; returns "Unknown error" if the code is out of bounds.

### `AliroError AliroError::FromInt(int ec)`
`modules/woz_aliro_stack/src/aliro_stack.cpp:113`

Convert an integer error code to an AliroError object. Returns ALIRO_ERROR_UNKNOWN if the code is
out of bounds (negative or >= ALIRO_ERROR_MAX); otherwise returns the corresponding error enum
value.

<details><summary>Undocumented (19)</summary>

- `IsDigit`
- `ParseDecimal`
- `IsLeapYear`
- `IsValidDate`
- `FromTimestamp`
- `SetVersion`
- `SetNotification`
- `SetTxPowerLevel`
- `SetTruncatedReaderGroupId`
- `SetTruncatedReaderGroupSubId`
- `SetDynamicTagExpiryTimestamp`
- `SetDynamicTag`
- `Init`
- `GetLibraryVersion`
- `GetExpeditedStandardProtocolVersions`
- `GetFeatures`
- `GenerateAdvertisingData`
- `GetBleAdvertisingVersion`
- `GetBleUwbProtocolVersions`

</details>
