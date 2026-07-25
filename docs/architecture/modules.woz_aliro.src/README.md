<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/`

| subsystem | about |
|---|---|
| [`modules/woz_aliro/src/aliro_advtag.c`](aliro_advtag.c.md) | Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1), shared by the |
| [`modules/woz_aliro/src/aliro_apdu.c`](aliro_apdu.c.md) | Aliro APDU TLV codec: builds command payloads (AUTH0, AUTH1, AuthData, EXCHANGE) and parses |
| [`modules/woz_aliro/src/aliro_apdu.h`](aliro_apdu.h.md) | APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a |
| [`modules/woz_aliro/src/aliro_approach.c`](aliro_approach.c.md) | *first commit: "approach: predict time-of-arrival, open the bolt at arrival"* |
| [`modules/woz_aliro/src/aliro_ble_central.c`](aliro_ble_central.c.md) | Platform-free half of the device-side BLE transport declared in |
| [`modules/woz_aliro/src/aliro_crypto.c`](aliro_crypto.c.md) | Aliro cryptographic primitives: key derivation (KDF/HKDF), key-block splitting, AES-GCM secure |
| [`modules/woz_aliro/src/aliro_device.c`](aliro_device.c.md) | Aliro initiator (User-Device) session machine: the implementation behind |
| [`modules/woz_aliro/src/aliro_device_apdu.c`](aliro_device_apdu.c.md) | Implementation of the device-side Access-Protocol wire codec declared in |
| [`modules/woz_aliro/src/aliro_hash.c`](aliro_hash.c.md) | Self-contained SHA-256, HMAC-SHA256, HKDF, and ANSI-X9.63 KDF implementation for the ESP32-IDF |
| [`modules/woz_aliro/src/aliro_hash.h`](aliro_hash.h.md) | Streaming SHA-256 (FIPS 180-4) implementation used by the Aliro crypto layer. |
| [`modules/woz_aliro/src/aliro_lat.c`](aliro_lat.c.md) | Walk-up latency trace: first-hit phase timestamps + the consolidated budget line. |
| [`modules/woz_aliro/src/aliro_prim_psa.c`](aliro_prim_psa.c.md) | Aliro crypto primitive backend implemented on Arm PSA Crypto: random generation, AES-256-GCM |
| [`modules/woz_aliro/src/aliro_prov.c`](aliro_prov.c.md) | Aliro reader provisioning state: default dev identity, and serialization/deserialization of the |
| [`modules/woz_aliro/src/aliro_ranging.c`](aliro_ranging.c.md) | UWB ranging bring-up and lifecycle for the Aliro reader: initializes the reader's UWB |
| [`modules/woz_aliro/src/aliro_ranging.h`](aliro_ranging.h.md) | Aliro M1-M4 ranging-setup interface: negotiates UWB ranging parameters with the device and |
| [`modules/woz_aliro/src/aliro_reader.c`](aliro_reader.c.md) | Aliro reader engine: drives the Access Protocol (AUTH0/AUTH1/EXCHANGE) handshake over BLE, |
| [`modules/woz_aliro/src/aliro_rssi_gate.c`](aliro_rssi_gate.c.md) | BLE-RSSI ranging power gate implementation: EWMA smoothing in Q4 fixed point, |
| [`modules/woz_aliro/src/aliro_stepup.c`](aliro_stepup.c.md) | Aliro step-up phase codec + verifier: derives the StepUpSK SessionData keys, builds the mdoc |
| [`modules/woz_aliro/src/aliro_stepup_parse.c`](aliro_stepup_parse.c.md) | DeviceResponse structural decoder for the Aliro step-up phase: a minimal, bounds-checked, |
