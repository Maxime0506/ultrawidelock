# Aliro source stack

This Zephyr module reimplements the public API declared by the Nordic Aliro headers.
Wire behavior follows the published Aliro 1.0 specification, and no Nordic source is
present in this tree.

The credential-auth facts are cited to specification section and line in
[`docs/esp32-gotchas.md` section 4](../../docs/esp32-gotchas.md), which also records the
field-of-use terms a downstream user inherits.

The existing nRF application uses this implementation by default:

```sh
make build
```

The source build removes the imported `aliro` archive from Zephyr's link list while
retaining the add-on's public headers and application integration. The build then
rejects any link map in which `libaliro_ble.a` contributed code. For regression
isolation only, `make build ALIRO_SOURCE=0` selects the legacy Nordic archive.

## Testing

1. `make test` runs the host known-answer and parser suites for the C protocol
   layer without NCS or hardware.
2. `make rebuild` compiles the complete nRF source stack. Its post-link check
   proves that `libaliro_ble.a` supplied no linked member.
3. Flash that image and run the nRF rows in
   [`docs/hardware-validation.md`](../../docs/hardware-validation.md). The boot
   console must include `Aliro source stack enabled`; only the phone tests verify
   NFC authentication and BLE/UWB session behavior end to end.

Implemented:

- library version, feature bitmap, and Aliro 1.0 protocol version lists;
- error conversion/string helpers and fixed-width RFC 3339 timestamp parsing;
- BLE advertising field setters and advertisement version 0;
- Aliro service data generation, including the AES-128 dynamic-tag input and
  truncation rules from section 11.3.1.
- bounded nRF session allocation and strict NFC Aliro AID selection/protocol
  negotiation, including BER/DER-TLV validation.
- byte-exact AUTH0 and AUTH1 APDU generation/parsing against the published
  expedited-standard and expedited-fast transcripts;
- expedited-standard ECDH/X9.63 and HKDF key schedules, Reader and User Device
  signature processing, AES-256-GCM secure response handling, Kpersistent
  derivation/preservation, and access-manager dispatch;
- expedited-fast Kpersistent enumeration, key derivation, constant-shape
  cryptogram trial decryption, access dispatch, and standards-defined fallback
  to expedited-standard;
- optional Reader certificate embedding in AUTH1.
- step-up session-key derivation and AES-256-GCM SessionData framing;
- ISO 18013-5 NFC DO53, ENVELOPE command chaining, extended-length APDUs,
  `61xx`, and GET RESPONSE handling;
- compact-key DeviceRequest and DeviceResponse/Access Document parsing,
  IssuerSignedItem digest checks, COSE_Sign1 issuer authentication, device-key
  binding, and Access Document validity checks.
- BLE Aliro-message framing, including concatenated L2CAP messages;
- mandatory URSK-availability EXCHANGE, directional BleSK derivation,
  authenticated message counters, and access-completed status;
- encrypted UWB/notification forwarding between BLE and the UWB adapter, plus
  encrypted reader-status updates.

The dynamic-tag byte order is pinned by all three published Appendix 20 test
vectors in `tests/host/test_aliro_advertising.c`.

Not implemented yet:

- timeout enforcement and optional supplementary/time-sync procedures;
- Bluetooth LE-only RKE flow.

The feature bitmap advertises the BLE/UWB flow when the application enables it.
