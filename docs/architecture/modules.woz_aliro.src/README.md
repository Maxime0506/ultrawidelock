<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/`

| subsystem | about |
|---|---|
| [`modules/woz_aliro/src/aliro_advtag.c`](aliro_advtag.c.md) | Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1), shared by the |
| [`modules/woz_aliro/src/aliro_apdu.c`](aliro_apdu.c.md) | Aliro APDU TLV codec: builds command payloads (AUTH0, AUTH1, AuthData, EXCHANGE) and parses |
| [`modules/woz_aliro/src/aliro_apdu.h`](aliro_apdu.h.md) | APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a |
| [`modules/woz_aliro/src/aliro_approach.c`](aliro_approach.c.md) | @file aliro_approach.c |
| [`modules/woz_aliro/src/aliro_assert.c`](aliro_assert.c.md) | Presence-assertion wire codec + verifier (see aliro_assert.h). Serialises a |
| [`modules/woz_aliro/src/aliro_assert_ec.c`](aliro_assert_ec.c.md) | Binds the aliro_assert P-256 seam to aliro_prim's ECDSA (see aliro_assert_ec.h). |
| [`modules/woz_aliro/src/aliro_crypto.c`](aliro_crypto.c.md) | Aliro cryptographic primitives: key derivation (KDF/HKDF), key-block splitting, AES-GCM secure |
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
