# Aliro source stack

This Zephyr module is a clean-room implementation of the public API declared by
the Nordic Aliro headers. Wire behavior is derived from Aliro Specification 1.0.
It does not use implementation details from Nordic's proprietary archive.

Enable it for the existing nRF application with:

```sh
make build ALIRO_SOURCE=1
```

The source option removes the imported `aliro` archive from Zephyr's link list,
while retaining the add-on's public headers and application integration. The
build then rejects any link map in which `libaliro_ble.a` contributed code.

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
