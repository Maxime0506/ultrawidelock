<!-- generated documentation — edit the source, not this file -->
# openaliro — architecture

Every subsystem on one page, in reading order: entry points (nothing imports them) first, then the machinery they drive. Each section is the subsystem's own prose, what it exposes, and how the pieces depend on each other; headings link to the full per-module reference under [`architecture/`](architecture/).

```mermaid
flowchart LR
  host.presence --> tools
  integration.homeassistant --> tools.tui.src
  integration.homeassistant.src.openaliro_ha --> tools.tui.src
  modules.woz_aliro.src --> modules.woz_aliro.include
  modules.woz_aliro.src --> modules.woz_port.include
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_aliro.src --> modules.woz_uwb.src.facade
  modules.woz_aliro_stack.src --> modules.woz_aliro_stack.src.protocol
  modules.woz_nfc.src --> modules.woz_nfc.include.woz_nfc
  modules.woz_uwb.src.aliro --> modules.woz_port.include
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.aliro.include.aliro_uwb_adapter --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_port.include
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.driver --> modules.woz_port.include
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.facade --> modules.woz_port.include
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.fira --> modules.woz_port.include
  modules.woz_uwb.src.fira --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.fira
  ports.esp32.apps.matter-lock.main --> ports.esp32.apps.matter-lock.main.lock
  ports.esp32.apps.matter-lock.main --> ports.esp32.components.aliro_reader
  ports.esp32.apps.matter-lock.main --> ports.esp32.components.piv_ccid.include
  ports.esp32.apps.reader.main --> ports.esp32.components.aliro_reader
  ports.esp32.components.piv_ccid --> ports.esp32.components.aliro_reader
  ports.esp32.components.piv_ccid --> ports.esp32.components.piv_ccid.include
  tools --> tools.tui.src
```

## `modules/woz_uwb/src/aliro/`

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md)

@file aliro_uwb_msg.c — setup/notification message codec.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/ccc/aliro_round_config.h`](architecture/modules.woz_uwb.src.ccc/aliro_round_config.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_util.h`](architecture/modules.woz_uwb.src.facade/woz_util.h.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

@file aliro_uwb_session.c — per-session lifecycle and state machine.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md)

@file aliro_uwb_adapter.c — reader-context lifecycle.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.c.md)

@file aliro_uwb_msg_builder.c — big-endian TLV message builder.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.c.md)

@file aliro_uwb_msg_parser.c — TLV attribute parser and big-endian reads.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md)

@file aliro_uwb_msg.h — message framing accessors, dispatch and builders.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md)

@file aliro_uwb_msg_builder.h — big-endian TLV message builder.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md)

@file aliro_uwb_msg_parser.h — TLV attribute iteration and big-endian reads.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md)

@file aliro_uwb_msg_spec.h — UWB ranging-service framing constants.

**used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md)

@file aliro_uwb_internal.h — private context types and shared helpers.

**depends on** [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

## `modules/woz_aliro/src/`

### [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md)

UWB ranging bring-up and lifecycle for the Aliro reader: initializes the reader's UWB
adapter and Cherry CCC context once, then arms, feeds, and tears down per-connection ranging
sessions driven by the M1-M4 setup exchanged over the peer's L2CAP channel.
Maintains process-wide singletons for the Cherry context and adapter (set up once via
aliro_ranging_init) and for the single active ranging session (the DW3000 supports only one
session at a time), tracking its owning secure channel for send/receive framing.

**depends on** [`modules/woz_aliro/include/aliro_ble.h`](architecture/modules.woz_aliro.include/aliro_ble.h.md), [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md), [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md), [`modules/woz_aliro/src/aliro_ranging.h`](architecture/modules.woz_aliro.src/aliro_ranging.h.md), [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

### [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

Aliro reader engine: drives the Access Protocol (AUTH0/AUTH1/EXCHANGE) handshake over BLE,
manages reader identity and credential trust provisioning in NVS, and arms UWB ranging once
a session is authenticated. Maintains a fixed-size table of per-connection sessions tracking
transaction phase and secure-channel state, and exposes start/attach entry points for both
standalone and Matter-attached BLE transports, plus provisioning and diagnostic APIs used by
Matter commissioning and the bench console.

**depends on** [`modules/woz_aliro/include/aliro_ble.h`](architecture/modules.woz_aliro.include/aliro_ble.h.md), [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md), [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/include/aliro_prov.h`](architecture/modules.woz_aliro.include/aliro_prov.h.md), [`modules/woz_aliro/include/aliro_reader.h`](architecture/modules.woz_aliro.include/aliro_reader.h.md), [`modules/woz_aliro/include/aliro_rssi_gate.h`](architecture/modules.woz_aliro.include/aliro_rssi_gate.h.md), [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md), [`modules/woz_aliro/src/aliro_apdu.h`](architecture/modules.woz_aliro.src/aliro_apdu.h.md), [`modules/woz_aliro/src/aliro_ranging.h`](architecture/modules.woz_aliro.src/aliro_ranging.h.md), [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)

### [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md)

Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.

**depends on** [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md), [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md), [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)

### [`modules/woz_aliro/src/aliro_assert_ec.c`](architecture/modules.woz_aliro.src/aliro_assert_ec.c.md)

Binds the aliro_assert P-256 seam to aliro_prim's ECDSA (see aliro_assert_ec.h).
The only file in the presence path with a crypto-backend dependency, which is
exactly why it is separate: aliro_assert.c keeps its cbmc and fuzz harnesses.

**depends on** [`modules/woz_aliro/include/aliro_assert_ec.h`](architecture/modules.woz_aliro.include/aliro_assert_ec.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)

### [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md)

Aliro cryptographic primitives: key derivation (KDF/HKDF), key-block splitting, AES-GCM secure
channels, and wire message framing built on a pluggable crypto backend (aliro_prim_*).
Implements the Aliro key-derivation chain (ECDH shared secret -> z -> 160-byte key block -> split
session keys / URSK / BLE ranging keys), per-direction AES-256-GCM secure channels with monotonic
message counters, and the seal/open framing used to carry engine plaintext over the wire.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

### [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md)

Aliro step-up phase codec + verifier: derives the StepUpSK SessionData keys, builds the mdoc
DeviceRequest and its ENVELOPE/GET RESPONSE APDUs, seals/opens SessionData over the aliro_secchan
AES-256-GCM channel, and runs the six-step Access Document verification of spec 7.4. The ES256
primitive is injected (verify ctx) so this unit carries no elliptic-curve dependency.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md), [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

### [`modules/woz_aliro/src/aliro_advtag.c`](architecture/modules.woz_aliro.src/aliro_advtag.c.md)

Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1), shared by the
BLE transport (live advertising) and the host KAT suite (spec section 20 worked examples).

**depends on** [`modules/woz_aliro/include/aliro_advtag.h`](architecture/modules.woz_aliro.include/aliro_advtag.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)

### [`modules/woz_aliro/src/aliro_assert.c`](architecture/modules.woz_aliro.src/aliro_assert.c.md)

Presence-assertion wire codec + verifier (see aliro_assert.h). Serialises a
dongle's "credential present within N cm for this nonce" statement and verifies
an ECDSA-P256 frame against a challenge nonce, enrolled credential and distance
threshold. Portable C11; no UWB/BLE/platform dependencies.

**depends on** [`modules/woz_aliro/include/aliro_assert.h`](architecture/modules.woz_aliro.include/aliro_assert.h.md), [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

### [`modules/woz_aliro/src/aliro_stepup_parse.c`](architecture/modules.woz_aliro.src/aliro_stepup_parse.c.md)

DeviceResponse structural decoder for the Aliro step-up phase: a minimal, bounds-checked,
depth-limited CBOR reader (definite-length core-deterministic only) plus the Table 8-22/7-1/7-2
field walk. No crypto and no allocation; every parsed field is a slice of the caller's buffer.
This is the wire-facing attack surface and is fuzzed on its own (tests/host/fuzz/fuzz_stepup.c).

**depends on** [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md)

### [`modules/woz_aliro/src/aliro_apdu.c`](architecture/modules.woz_aliro.src/aliro_apdu.c.md)

Aliro APDU TLV codec: builds command payloads (AUTH0, AUTH1, AuthData, EXCHANGE) and parses
response APDUs, plus BLE envelope framing/unframing and ISO7816 APDU wrap/status-word stripping.
Provides a minimal BER-TLV writer (aliro_tlv_w_init/put/finish) used to assemble command
payloads, and TLV/APDU parsing helpers used to extract fields from device responses.

**depends on** [`modules/woz_aliro/src/aliro_apdu.h`](architecture/modules.woz_aliro.src/aliro_apdu.h.md)

### [`modules/woz_aliro/src/aliro_approach.c`](architecture/modules.woz_aliro.src/aliro_approach.c.md)

@file aliro_approach.c
Kalman-filtered approach controller for predictive unlock. Tracks distance (cm), velocity (cm/s),
and estimated time-to-arrival (ms) at the unlock radius. Supervises presence via median filtering
of trusted ranges and fires predictive unlock when closing speed and ETA meet thresholds. Factory
defaults: unlock 100 cm, relock 250 cm, dwell times 2 s and 3 s, motor delay 500 ms, margin 250
ms, velocity floor 30 cm/s, prediction enabled.

**depends on** [`modules/woz_aliro/include/aliro_approach.h`](architecture/modules.woz_aliro.include/aliro_approach.h.md)

### [`modules/woz_aliro/src/aliro_hash.c`](architecture/modules.woz_aliro.src/aliro_hash.c.md)

Self-contained SHA-256, HMAC-SHA256, HKDF, and ANSI-X9.63 KDF implementation for the ESP32-IDF
Aliro crypto port, with no external crypto library dependency.

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

### [`modules/woz_aliro/src/aliro_prim_psa.c`](architecture/modules.woz_aliro.src/aliro_prim_psa.c.md)

Aliro crypto primitive backend implemented on Arm PSA Crypto: random generation, AES-256-GCM
encrypt/decrypt, and NIST P-256 key generation, ECDH, and ECDSA sign/verify.
Provides the aliro_prim_* / aliro_* primitive functions consumed by the higher-level Aliro KDF
and secure-channel code in aliro_crypto.c; callers must call aliro_prim_init before using any
other function in this file.

**depends on** [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)

### [`modules/woz_aliro/src/aliro_prov.c`](architecture/modules.woz_aliro.src/aliro_prov.c.md)

Aliro reader provisioning state: default dev identity, and serialization/deserialization of the
reader identity plus trusted-credential store to/from a self-describing binary blob.
Also implements the trust-store membership check and add-with-dedup operations used to decide
whether a presented credential public key is trusted.

**depends on** [`modules/woz_aliro/include/aliro_prov.h`](architecture/modules.woz_aliro.include/aliro_prov.h.md)

### [`modules/woz_aliro/src/aliro_rssi_gate.c`](architecture/modules.woz_aliro.src/aliro_rssi_gate.c.md)

BLE-RSSI ranging power gate implementation: EWMA smoothing in Q4 fixed point,
open/close hysteresis with a sustained-below close hold, and an optional
rise-rate fast open so a fast approach is not penalized by the smoothing lag.
Pure logic — no radio, clock, or logging dependencies — so the host suite can
drive it with synthetic approach traces.

**depends on** [`modules/woz_aliro/include/aliro_rssi_gate.h`](architecture/modules.woz_aliro.include/aliro_rssi_gate.h.md)

### [`modules/woz_aliro/src/aliro_ranging.h`](architecture/modules.woz_aliro.src/aliro_ranging.h.md)

Aliro M1-M4 ranging-setup interface: negotiates UWB ranging parameters with the device and
produces the BLE ranging-control secure channel used to carry the M1-M4 exchange.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/src/aliro_apdu.h`](architecture/modules.woz_aliro.src/aliro_apdu.h.md)

APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a
TLV writer and parses the AUTH0/AUTH1 response APDUs exchanged during the reader-device
handshake.

**used by** [`modules/woz_aliro/src/aliro_apdu.c`](architecture/modules.woz_aliro.src/aliro_apdu.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

Streaming SHA-256 (FIPS 180-4) implementation used by the Aliro crypto layer.
Declares struct aliro_sha256, the incremental hash context used across init/update/finish
calls.

**used by** [`modules/woz_aliro/src/aliro_assert.c`](architecture/modules.woz_aliro.src/aliro_assert.c.md), [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_hash.c`](architecture/modules.woz_aliro.src/aliro_hash.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md)

## `modules/woz_uwb/src/ccc/`

### [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md)

@file ccc_shim_rx.c — responder-RX CCC STS substitution (ld --wrap=dwt_rxenable) programming the
CCC STS on each RX-arm; target only.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/ccc/aliro_round_config.h`](architecture/modules.woz_uwb.src.ccc/aliro_round_config.h.md), [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md), [`modules/woz_uwb/src/ccc/ccc_mac.h`](architecture/modules.woz_uwb.src.ccc/ccc_mac.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](architecture/modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/woz_bytes.h`](architecture/modules.woz_uwb.src.facade/woz_bytes.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

### [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

@file cherry_ccc_shim.c — cherry_ccc_* seam (Aliro responder) implemented over the lock-native
FiRa MAC; maps each call onto woz_uwb_facade.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md), [`modules/woz_uwb/src/ccc/aliro_round_config.h`](architecture/modules.woz_uwb.src.ccc/aliro_round_config.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_util.h`](architecture/modules.woz_uwb.src.facade/woz_util.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

### [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md)

@file ccc_shim_wrap.c — per-frame STS interception (ld --wrap=dwt_configurestsiv) substituting
CCC STS for the FiRa MAC; target only.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/facade/woz_bytes.h`](architecture/modules.woz_uwb.src.facade/woz_bytes.h.md)

### [`modules/woz_uwb/src/ccc/ccc_session.c`](architecture/modules.woz_uwb.src.ccc/ccc_session.c.md)

@file ccc_session.c — Aliro/CCC ranging seam implementation. See ccc_session.h.

**depends on** [`modules/woz_uwb/src/ccc/ccc_session.h`](architecture/modules.woz_uwb.src.ccc/ccc_session.h.md)

### [`modules/woz_uwb/src/ccc/ccc_sts.c`](architecture/modules.woz_uwb.src.ccc/ccc_sts.c.md)

@file ccc_sts.c — DW3000 STS register load for the CCC ranging path.

**depends on** [`modules/woz_uwb/src/ccc/ccc_sts.h`](architecture/modules.woz_uwb.src.ccc/ccc_sts.h.md), [`modules/woz_uwb/src/facade/woz_bytes.h`](architecture/modules.woz_uwb.src.facade/woz_bytes.h.md)

### [`modules/woz_uwb/src/ccc/ccc_mac.c`](architecture/modules.woz_uwb.src.ccc/ccc_mac.c.md)

@file ccc_mac.c — UWB MAC: hopping sequence, SP0 frame codec, ranging schedule.

**depends on** [`modules/woz_uwb/src/ccc/ccc_mac.h`](architecture/modules.woz_uwb.src.ccc/ccc_mac.h.md)

### [`modules/woz_uwb/src/ccc/ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim.c.md)

@file ccc_shim.c — CCC STS substitution core (implementation).

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md)

### [`modules/woz_uwb/src/ccc/ccc_crypto_mbedtls.c`](architecture/modules.woz_uwb.src.ccc/ccc_crypto_mbedtls.c.md)

@file ccc_crypto_mbedtls.c — AES-ECB block via mbedTLS, backing the CCC key schedule on SoCs
without a PSA provider (e.g. ESP32-S3).

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)

### [`modules/woz_uwb/src/ccc/ccc_crypto_psa.c`](architecture/modules.woz_uwb.src.ccc/ccc_crypto_psa.c.md)

@file ccc_crypto_psa.c — On-target AES-ECB block (PSA/CC312) backing the CCC key schedule.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)

### [`modules/woz_uwb/src/ccc/ccc_kdf.c`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.c.md)

@file ccc_kdf.c — UWB key schedule + SP0 Pre-POLL frame codec.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)

### [`modules/woz_uwb/src/ccc/aliro_round_config.h`](architecture/modules.woz_uwb.src.ccc/aliro_round_config.h.md)

@file aliro_round_config.h — one knob for the CCC ranging round's responder count.

**used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

### [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)

@file ccc_kdf.h
@brief UWB ranging key schedule + SP0 frame crypto (CONFIG_WOZ_ALIRO).
Turns the 32-byte URSK into the per-ranging-cycle keys the DW3000 STS engine
and the SP0 frames consume, over a single AES block-encrypt primitive.

**used by** [`modules/woz_uwb/src/ccc/ccc_crypto_mbedtls.c`](architecture/modules.woz_uwb.src.ccc/ccc_crypto_mbedtls.c.md), [`modules/woz_uwb/src/ccc/ccc_crypto_psa.c`](architecture/modules.woz_uwb.src.ccc/ccc_crypto_psa.c.md), [`modules/woz_uwb/src/ccc/ccc_kdf.c`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.c.md), [`modules/woz_uwb/src/ccc/ccc_mac.h`](architecture/modules.woz_uwb.src.ccc/ccc_mac.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_sts.h`](architecture/modules.woz_uwb.src.ccc/ccc_sts.h.md)

### [`modules/woz_uwb/src/ccc/ccc_mac.h`](architecture/modules.woz_uwb.src.ccc/ccc_mac.h.md)

@file ccc_mac.h — CCC UWB MAC layer: ranging-round scheduling, SP0 frame codec, DS-TWR.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_mac.c`](architecture/modules.woz_uwb.src.ccc/ccc_mac.c.md), [`modules/woz_uwb/src/ccc/ccc_session.h`](architecture/modules.woz_uwb.src.ccc/ccc_session.h.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md)

### [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md)

@file ccc_shim.h — map a per-frame STS index to the (dURSK, STS-V) pair the DW3000 STS engine
loads.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/driver/uwb_selftest.c`](architecture/modules.woz_uwb.src.driver/uwb_selftest.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/ccc/aliro_kdf.h`](architecture/modules.woz_uwb.src.ccc/aliro_kdf.h.md)

@file aliro_kdf.h — UWB Ranging Secret Key (URSK) length.

**used by** [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md), [`modules/woz_uwb/src/fira/fira_session.c`](architecture/modules.woz_uwb.src.fira/fira_session.c.md)

### [`modules/woz_uwb/src/ccc/ccc_session.h`](architecture/modules.woz_uwb.src.ccc/ccc_session.h.md)

@file ccc_session.h — Aliro/CCC ranging seam: map an Aliro session's URSK + M1-M4 setup to
ccc_ran_params.

**depends on** [`modules/woz_uwb/src/ccc/ccc_mac.h`](architecture/modules.woz_uwb.src.ccc/ccc_mac.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_session.c`](architecture/modules.woz_uwb.src.ccc/ccc_session.c.md)

### [`modules/woz_uwb/src/ccc/ccc_sts.h`](architecture/modules.woz_uwb.src.ccc/ccc_sts.h.md)

@file ccc_sts.h — load a CCC ranging PPDU's STS key + IV into the DW3000 STS engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](architecture/modules.woz_uwb.src.ccc/ccc_kdf.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_sts.c`](architecture/modules.woz_uwb.src.ccc/ccc_sts.c.md)

## `tools/tui/src/`

### [`tools/tui/src/main.tsx`](architecture/tools.tui.src/main.tsx.md)

**depends on** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

**depends on** [`tools/tui/src/devices.ts`](architecture/tools.tui.src/devices.ts.md), [`tools/tui/src/jobs.ts`](architecture/tools.tui.src/jobs.ts.md), [`tools/tui/src/motion.ts`](architecture/tools.tui.src/motion.ts.md), [`tools/tui/src/search.ts`](architecture/tools.tui.src/search.ts.md), [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md), [`tools/tui/src/targets.ts`](architecture/tools.tui.src/targets.ts.md), [`tools/tui/src/terminal.ts`](architecture/tools.tui.src/terminal.ts.md), [`tools/tui/src/theme.ts`](architecture/tools.tui.src/theme.ts.md), [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md), [`tools/tui/src/wizard.ts`](architecture/tools.tui.src/wizard.ts.md)  ·  **used by** [`tools/tui/src/main.tsx`](architecture/tools.tui.src/main.tsx.md)

### [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md)

**used by** [`integration/homeassistant/aliro_mqtt_bridge.py`](architecture/integration.homeassistant/aliro_mqtt_bridge.md), [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_transport.md), [`tools/presence_git.py`](architecture/tools/presence_git.md), [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md), [`tools/tui/src/targets.ts`](architecture/tools.tui.src/targets.ts.md)

### [`tools/tui/src/devices.ts`](architecture/tools.tui.src/devices.ts.md)

**depends on** [`tools/tui/src/theme.ts`](architecture/tools.tui.src/theme.ts.md), [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/jobs.ts`](architecture/tools.tui.src/jobs.ts.md)

**depends on** [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/motion.ts`](architecture/tools.tui.src/motion.ts.md)

*No module docstring. First commit: "Give the bench TUI its labels back as border rules".*

**depends on** [`tools/tui/src/theme.ts`](architecture/tools.tui.src/theme.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/search.ts`](architecture/tools.tui.src/search.ts.md)

Searching the serial scrollback.
Pure string work, kept out of app.tsx so it can be tested directly instead of
through a rendered terminal. Matching is case-insensitive and literal: a
firmware log is full of `[`, `*`, `0x..` and `?`, so treating the query as a
regular expression would turn ordinary searches into syntax errors.

**used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/targets.ts`](architecture/tools.tui.src/targets.ts.md)

**depends on** [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md), [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md), [`tools/tui/src/wizard.ts`](architecture/tools.tui.src/wizard.ts.md)

### [`tools/tui/src/terminal.ts`](architecture/tools.tui.src/terminal.ts.md)

**used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

### [`tools/tui/src/theme.ts`](architecture/tools.tui.src/theme.ts.md)

**used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md), [`tools/tui/src/devices.ts`](architecture/tools.tui.src/devices.ts.md), [`tools/tui/src/motion.ts`](architecture/tools.tui.src/motion.ts.md)

### [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md)

**used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md), [`tools/tui/src/devices.ts`](architecture/tools.tui.src/devices.ts.md), [`tools/tui/src/jobs.ts`](architecture/tools.tui.src/jobs.ts.md), [`tools/tui/src/targets.ts`](architecture/tools.tui.src/targets.ts.md), [`tools/tui/src/wizard.ts`](architecture/tools.tui.src/wizard.ts.md)

### [`tools/tui/src/wizard.ts`](architecture/tools.tui.src/wizard.ts.md)

**depends on** [`tools/tui/src/targets.ts`](architecture/tools.tui.src/targets.ts.md), [`tools/tui/src/types.ts`](architecture/tools.tui.src/types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](architecture/tools.tui.src/app.tsx.md)

## `integration/homeassistant/src/openaliro_ha/`

### [`integration/homeassistant/src/openaliro_ha/__main__.py`](architecture/integration.homeassistant.src.openaliro_ha/__main__.md)

Module entry point for the HA=1-only staging command.

**depends on** [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md)

### [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md)

HA=1-only staging library for the OpenAliro Home Assistant adapters.

This is intentionally not a published distribution or a stable public API yet.
The direct Home Assistant adapter remains blocked on Stage 0 hardware evidence.

**depends on** [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/compatibility.py`](architecture/integration.homeassistant.src.openaliro_ha/compatibility.md), [`integration/homeassistant/src/openaliro_ha/config.py`](architecture/integration.homeassistant.src.openaliro_ha/config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](architecture/integration.homeassistant.src.openaliro_ha/mqtt.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md), [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_transport.md)

### [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md)

Small, non-interactive HA=1 staging CLI for safe offline operations.

**exposes** `main`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/config.py`](architecture/integration.homeassistant.src.openaliro_ha/config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md), [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_transport.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__main__.py`](architecture/integration.homeassistant.src.openaliro_ha/__main__.md)

### [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md)

Runnable standalone-agent orchestration over the shared serial library.

**exposes** `AgentError`, `DoctorDeviceResult`, `doctor`, `probe_device`, `run`, `run_device`, `session_for_device`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/config.py`](architecture/integration.homeassistant.src.openaliro_ha/config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](architecture/integration.homeassistant.src.openaliro_ha/mqtt.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md), [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_transport.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md)

### [`integration/homeassistant/src/openaliro_ha/compatibility.py`](architecture/integration.homeassistant.src.openaliro_ha/compatibility.md)

Incremental parser for the source-proven ``aliro range`` compatibility mode.

**exposes** `RangeResponseParser`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md)

### [`integration/homeassistant/src/openaliro_ha/config.py`](architecture/integration.homeassistant.src.openaliro_ha/config.md)

Versioned, secret-free TOML configuration for the HA=1-only agent.

**exposes** `AgentConfig`, `ConfigError`, `DeviceConfig`, `MqttConfig`, `load_config`, `redacted_config`, `write_config`  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](architecture/integration.homeassistant.src.openaliro_ha/mqtt.md)

### [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md)

Typed observations emitted by the HA=1 console parser.

**exposes** `AccessEvent`, `CompatibilityRangeReading`, `DistanceReading`  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md), [`integration/homeassistant/src/openaliro_ha/compatibility.py`](architecture/integration.homeassistant.src.openaliro_ha/compatibility.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](architecture/integration.homeassistant.src.openaliro_ha/mqtt.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md)

### [`integration/homeassistant/src/openaliro_ha/mqtt.py`](architecture/integration.homeassistant.src.openaliro_ha/mqtt.md)

Standalone MQTT adapter for the HA=1 staging agent.

The parser and serial session do not import this module. MQTT remains an agent
transport, with the current discovery and state topics kept stable.

**exposes** `MqttError`, `MqttPublisher`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/config.py`](architecture/integration.homeassistant.src.openaliro_ha/config.md), [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md)

### [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md)

Narrow parser for the currently verified nRF5340 console output.

**exposes** `parse_console_line`, `strip_ansi`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md), [`integration/homeassistant/src/openaliro_ha/compatibility.py`](architecture/integration.homeassistant.src.openaliro_ha/compatibility.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md)

### [`integration/homeassistant/src/openaliro_ha/serial_session.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_session.md)

Async, transport-neutral ownership of one OpenAliro serial console.

The session is deliberately independent of pyserial and Home Assistant. A
runtime adapter provides an opened byte-stream; this module serializes shell
commands, parses only the approved observations, and never retains raw console
lines. The device can be idle after ``aliro frames on``: a stream acknowledgement
is therefore the capability probe, not the first range reading.

**exposes** `SerialConnection`, `SerialSession`, `SerialSessionError`, `SessionState`  ·  **depends on** [`integration/homeassistant/src/openaliro_ha/compatibility.py`](architecture/integration.homeassistant.src.openaliro_ha/compatibility.md), [`integration/homeassistant/src/openaliro_ha/models.py`](architecture/integration.homeassistant.src.openaliro_ha/models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](architecture/integration.homeassistant.src.openaliro_ha/parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md)

### [`integration/homeassistant/src/openaliro_ha/serial_transport.py`](architecture/integration.homeassistant.src.openaliro_ha/serial_transport.md)

pyserial adapter and privacy-safe serial-port identity helpers.

**exposes** `PySerialConnection`, `SerialPort`, `SerialTransportError`, `discover_serial_ports`, `open_serial_connection`, `resolve_serial_port`, `serial_identity`  ·  **depends on** [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](architecture/integration.homeassistant.src.openaliro_ha/__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](architecture/integration.homeassistant.src.openaliro_ha/agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](architecture/integration.homeassistant.src.openaliro_ha/cli.md)

## `modules/woz_uwb/src/driver/`

### [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md)

@file uwb_rxdiag.c — Diagnostic RX/TX event tallies + ranging heartbeat.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](architecture/modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

### [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md)

@file uwb_isr.c — DW3000 interrupt-callback registration (implementation).

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/driver/uwb_isr.h`](architecture/modules.woz_uwb.src.driver/uwb_isr.h.md), [`modules/woz_uwb/src/facade/trace.h`](architecture/modules.woz_uwb.src.facade/trace.h.md)

### [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_cirdiag.c.md)

@file uwb_cirdiag.c — CIA RX-diagnostics latch + [ALAB] emitter (channel-impulse Stage 0/1).
Split the work across the two contexts the ALAB contract demands: the RX callback only
latches registers into a snapshot (uwb_cirdiag_capture, plain stores + one SPI read), and a
task-side uwb_cirdiag_flush formats/prints the line. On the nRF the flush runs on the
sysworkq (uwb_rxdiag.c submits it); on the ESP32 the pinned ISR-service task calls it after
its IRQ drain loop, so capture and flush are sequential there. A seqlock covers the
one real race (nRF: a new capture preempting a flush mid-copy): torn snapshots are dropped,
the next reception re-latches.
Stage 1 adds an independently-armed windowed-CIR dump: when armed, capture also reads a
fixed window of Ipatov complex taps centred on the first-path index into the snapshot. The
taps are NOT printed on the RX/flush path — a full window is ~64 serial lines per reception,
enough blocking UART to overrun the ranging slot and stall a live walk-up. Instead flush
appends each window to a small RAM ring (the last CIRDIAG_RING_RECS receptions), and the taps
are drained to `ev=uwb.cir` lines only when the dump is disarmed (uwb_cirdiag_dump_set_enabled
(false)) — that runs in console/task context after the walk-up, so the unlock is unaffected
while capturing. Deferring the printing was necessary but not sufficient: the window READ is
itself too long to sit inside a live ranging block, where the responder still owes a POLL or
Final reception. The shims pass that down as deadline_pending and the window is taken only on
the Final. Nor was that sufficient: the accumulator cannot be read at all while the receiver
is up, and the shim re-arms an SP0 listen the moment the Final is serviced, so the read has to
happen BEFORE that (the shims gate it on ccc_shim_rx_awaiting_final). Doing it on every block
then cost every range, so uwb_cirdiag_window_due decimates it to one Final in
CIRDIAG_CIR_EVERY.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](architecture/modules.woz_uwb.src.facade/uwb_cirdiag.h.md)

### [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md)

@file uwb_min.c — DW3110 bring-up driver (implementation).

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](architecture/modules.woz_uwb.src.driver/uwb_min.h.md)

### [`modules/woz_uwb/src/driver/uwb_selftest.c`](architecture/modules.woz_uwb.src.driver/uwb_selftest.c.md)

@file uwb_selftest.c — Kconfig-gated one-shot UWB init self-test (no iPhone).

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

### [`modules/woz_uwb/src/driver/uwb_min.h`](architecture/modules.woz_uwb.src.driver/uwb_min.h.md)

@file uwb_min.h — Minimal DW3110 (DWM3000EVB) hardware bring-up driver.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md)

@file uwb_rxdiag.h — Read-side accessors for the RX event tallies + log stream.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/driver/uwb_isr.h`](architecture/modules.woz_uwb.src.driver/uwb_isr.h.md)

@file uwb_isr.h — DW3000 interrupt-callback registration (public surface).

**used by** [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md)

## `modules/woz_uwb/src/shell/`

### [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](architecture/modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](architecture/modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md), [`modules/woz_uwb/src/shell/aliro_shell.h`](architecture/modules.woz_uwb.src.shell/aliro_shell.h.md)

### [`modules/woz_uwb/src/shell/aliro_shell.h`](architecture/modules.woz_uwb.src.shell/aliro_shell.h.md)

@file aliro_shell.h — the one seam the `aliro` console needs from the application.

**used by** [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

## `modules/woz_aliro_stack/src/`

### [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

@file session.cpp
Aliro reader BLE session state machine and cryptographic session context. Manages NFC APDU
limits, response timeouts, connection setup, fast-path and standard key derivation, message
encryption and decryption, and reader-status notifications. Processes events from the BLE
transport and application layer.

**depends on** [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md), [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md)

### [`modules/woz_aliro_stack/src/advertising_core.c`](architecture/modules.woz_aliro_stack.src/advertising_core.c.md)

@file advertising_core.c
Compute dynamic advertisement tag inputs and extract tags from AES ciphertext. The plaintext
input incorporates the device's BLE address and an expiry timestamp; the tag is derived by AES
encryption and truncation for inclusion in Aliro BLE advertisements per specification section 20.

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

### [`modules/woz_aliro_stack/src/aliro_stack.cpp`](architecture/modules.woz_aliro_stack.src/aliro_stack.cpp.md)

Independent implementation of the Nordic Aliro public API used by this app.
Protocol constants and wire formats come from Aliro Specification 1.0.

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

### [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

Aliro BLE advertising primitives.
Kept as portable C so the byte-order rules can be tested on the host using
the specification's published known-answer vectors.

**used by** [`modules/woz_aliro_stack/src/advertising_core.c`](architecture/modules.woz_aliro_stack.src/advertising_core.c.md), [`modules/woz_aliro_stack/src/aliro_stack.cpp`](architecture/modules.woz_aliro_stack.src/aliro_stack.cpp.md)

## `modules/woz_uwb/src/facade/`

### [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md)

UWB facade: binds the CCC credential-based STS engine to the DW3000 radio, exposes Aliro DS-TWR
responder start/stop and range query, and manages platform dependencies (HFCLK boost, SPI init,
callbacks).

**depends on** [`modules/woz_uwb/src/ccc/aliro_kdf.h`](architecture/modules.woz_uwb.src.ccc/aliro_kdf.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

### [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md)

@file flight_recorder.c
Binary flight-recorder format: framed records (magic, metadata, configuration, events, end) with
little-endian integers and truncation handling; read/write operations with overflow detection.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

### [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md)

Memory allocation and timing facade: qmalloc, qcalloc, qfree wrap the platform heap;
qrtc_get_us returns monotonic microseconds since boot.

**depends on** [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md)

### [`modules/woz_uwb/src/facade/woz_util.h`](architecture/modules.woz_uwb.src.facade/woz_util.h.md)

*No module docstring. First commit: "port: replace the Zephyr compat shims with a neutral woz_port.h contract".*

**used by** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

### [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

Public header for UWB facade: exposes Aliro DS-TWR responder lifecycle and range query; the CCC
engine is bound and unbound via internal ursk and stop calls.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_selftest.c`](architecture/modules.woz_uwb.src.driver/uwb_selftest.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md)

### [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md)

@file flight_recorder.h
Capture and replay UWB frames and session configuration from a walk-up to a host for analysis and
replay. Records endpoint identity, status registers, frame data, and timing metadata into a
fixed-size ring buffer; provides reader and writer interfaces for host tools.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/facade/woz_bytes.h`](architecture/modules.woz_uwb.src.facade/woz_bytes.h.md)

@file woz_bytes.h
Byte-order utilities: read/write 16-bit and 32-bit integers in little-endian or big-endian order.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/ccc/ccc_sts.c`](architecture/modules.woz_uwb.src.ccc/ccc_sts.c.md)

### [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md)

@file woz_diag.h — DIAGK(): gate for verbose UWB bring-up diagnostics.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md)

### [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](architecture/modules.woz_uwb.src.facade/uwb_cirdiag.h.md)

@file uwb_cirdiag.h — Per-reception CIA first-path/STS diagnostics stream (channel-impulse
Stage 0). The RX callback latches the DW3000's CIA diagnostic bank (Ipatov/STS first-path
index, F1..F3, power, peak, STS quality, xtal offset); task context emits it as one
"[ALAB] t=<us> ev=uwb.diag ..." line for tools/aliro_lab.py. OFF at boot; armed at runtime
(nRF `aliro cir on`, ESP32 rides the `lab on` gate).

**used by** [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/facade/trace.h`](architecture/modules.woz_uwb.src.facade/trace.h.md)

@file trace.h — Structured [WOZ_TRACE] emit helpers, gated on CONFIG_WOZ_E2E_TRACE.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md)  ·  **used by** [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md)

### [`modules/woz_uwb/src/facade/woz_logfmt.c`](architecture/modules.woz_uwb.src.facade/woz_logfmt.c.md)

@file woz_logfmt.c — PRETTY-gated high-res timestamp + compact colored log line.

### [`modules/woz_uwb/src/facade/woz_logquiet.c`](architecture/modules.woz_uwb.src.facade/woz_logquiet.c.md)

@file woz_logquiet.c — PRETTY-gated runtime muting of benign upstream error spam.
The stock Matter/BLE stack logs several non-fatal conditions at LOG_ERR/LOG_WRN
(red/yellow): mDNS advertiser "incorrect state" churn, "Long dispatch time"
perf notes, unsupported-attribute reads, the "No valid legacy adv to stop" BLE
double-stop, and the empty-slot "Failed to get Access Document at index: 0" the
access layer emits on first contact. All are expected on this bare DK bring-up
and every one is proven benign by the healthy unlock that follows.
A compile-time level cut can't remove just these: each noisy source shares its
CONFIG_*_LOG_LEVEL with a source whose INFO lines drive the demo narrative
(access_document shares CONFIG_DOOR_LOCK_APP_LOG_LEVEL with access_manager's
"ACCESS GRANTED"/ranging lines; bt_adv shares CONFIG_BT_HCI_CORE_LOG_LEVEL),
and a threshold below ERR still lets ERR through. So mute per-source at runtime.
Reversible: compiled only under CONFIG_WOZ_PRETTY_SHELL (PRETTY=1). Drop PRETTY
and every one of these lines returns for raw diagnosis. Needs
CONFIG_LOG_RUNTIME_FILTERING=y (set in ports/nrf5340dk/overlays/woz-pretty.conf).

## `ports/esp32/apps/matter-lock/main/`

### [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md)

Matter application main: door lock endpoint setup, Matter lifecycle event handling, and (when
CONFIG_ENABLE_ALIRO_BLE_UWB is set) startup/coexistence wiring for the Aliro BLE+UWB reader
alongside the Matter BLE commissioning transport.
Owns the Aliro reader background task (started once on commissioning-complete or at boot if
already commissioned) and the Matter attribute/identify/device-event callbacks required by
esp-matter's node/cluster framework.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](architecture/ports.esp32.apps.matter-lock.main/app_priv.h.md), [`ports/esp32/apps/matter-lock/main/app_shell.h`](architecture/ports.esp32.apps.matter-lock.main/app_shell.h.md), [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.h.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md), [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md), [`ports/esp32/components/piv_ccid/include/piv_ccid_usb.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid_usb.h.md)

### [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md)

ESP32-IDF console shell for the Aliro Matter door lock app: registers status, range, aliro, lock/unlock, codes, factoryreset, and clear commands and runs the REPL.

**depends on** [`ports/esp32/apps/matter-lock/main/app_shell.h`](architecture/ports.esp32.apps.matter-lock.main/app_shell.h.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md), [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md)

### [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_driver.cpp.md)

Board driver glue for the ESP32 Matter port: button input, WS2812 lock-status LED, and the
Matter attribute-update hook wired into the app's driver layer.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](architecture/ports.esp32.apps.matter-lock.main/app_priv.h.md), [`ports/esp32/apps/matter-lock/main/lock_led.h`](architecture/ports.esp32.apps.matter-lock.main/lock_led.h.md)

### [`ports/esp32/apps/matter-lock/main/lock_led.c`](architecture/ports.esp32.apps.matter-lock.main/lock_led.c.md)

Lock-state indicator LED: maps lock state (and Aliro activity) to an RGB colour for the single
status pixel.
Locked always extinguishes the indicator; unlocked shows blue during active UWB/Aliro engagement
and a different colour otherwise, per lock_led_color.

**depends on** [`ports/esp32/apps/matter-lock/main/lock_led.h`](architecture/ports.esp32.apps.matter-lock.main/lock_led.h.md)

### [`ports/esp32/apps/matter-lock/main/app_priv.h`](architecture/ports.esp32.apps.matter-lock.main/app_priv.h.md)

**used by** [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_driver.cpp.md), [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md)

### [`ports/esp32/apps/matter-lock/main/app_shell.h`](architecture/ports.esp32.apps.matter-lock.main/app_shell.h.md)

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md)

### [`ports/esp32/apps/matter-lock/main/lock_led.h`](architecture/ports.esp32.apps.matter-lock.main/lock_led.h.md)

Lock status LED color mapping: derives the RGB color for the lock indicator from the
current locked and Aliro-ranging state.

**used by** [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_driver.cpp.md), [`ports/esp32/apps/matter-lock/main/lock_led.c`](architecture/ports.esp32.apps.matter-lock.main/lock_led.c.md)

## `host/presence/`

### [`host/presence/presence-run`](architecture/host.presence/presence-run.md)

*No module docstring. First commit: "presence: add local daemon and command gate".*

**depends on** [`host/presence/presence_client.py`](architecture/host.presence/presence_client.md)

### [`host/presence/presence-enroll`](architecture/host.presence/presence-enroll.md)

*No module docstring. First commit: "presence: add local daemon and command gate".*

**depends on** [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md)

### [`host/presence/presenced`](architecture/host.presence/presenced.md)

*No module docstring. First commit: "presence: add local daemon and command gate".*

**depends on** [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md)

### [`host/presence/presence_client.py`](architecture/host.presence/presence_client.md)

Client and command gate for the local presenced Unix socket.

**depends on** [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md)  ·  **used by** [`host/presence/presence-run`](architecture/host.presence/presence-run.md)

### [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md)

Fresh, pinned presence proofs behind an owner-only Unix socket.

**depends on** [`tools/presence_git.py`](architecture/tools/presence_git.md), [`tools/presence_verify.py`](architecture/tools/presence_verify.md)  ·  **used by** [`host/presence/presence-enroll`](architecture/host.presence/presence-enroll.md), [`host/presence/presence_client.py`](architecture/host.presence/presence_client.md), [`host/presence/presenced`](architecture/host.presence/presenced.md)

## `modules/woz_nfc/src/`

### [`modules/woz_nfc/src/transport_pn532.cpp`](architecture/modules.woz_nfc.src/transport_pn532.cpp.md)

WozNfc backend driving an NXP PN532 reader.
A dedicated thread owns the chip: it runs the discovery loop (RF field on,
one Apple ECP broadcast, one 106 kbps type A activation attempt, field off,
sleep) and, once an ISO-DEP User Device is activated, performs the blocking
APDU round trips. Stack callbacks (CreateSession / HandleSessionData /
DestroySession) are posted to the Aliro workqueue so the stack observes the
same threading as with the upstream RFAL transport, and Send() stays
asynchronous: it hands the APDU to the thread and returns.
The ECP frame layout mirrors modules/woz_aliro_ecp (the RFAL-path emitter):
8-byte Aliro ECP v2 header, 8-byte provisioned reader identifier, CRC_A.
The PN532 cannot inject raw frames mid-discovery the way RFAL's proprietary
poll hook can, so the frame is broadcast with InCommunicateThru while the
CIU CRC is switched off, between activation attempts — the same cadence a
matching iPhone expects: ECP beacon, then WUPA.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](architecture/modules.woz_nfc.include.woz_nfc/transport.h.md), [`modules/woz_nfc/src/pn532.h`](architecture/modules.woz_nfc.src/pn532.h.md), [`modules/woz_nfc/src/pn532_apdu.h`](architecture/modules.woz_nfc.src/pn532_apdu.h.md), [`modules/woz_nfc/src/pn532_bus.h`](architecture/modules.woz_nfc.src/pn532_bus.h.md)

### [`modules/woz_nfc/src/pn532_bus_spi.c`](architecture/modules.woz_nfc.src/pn532_bus_spi.c.md)

Zephyr SPI glue for the PN532 host protocol.
PN532 SPI framing (UM0701-02 §6.2.5): every transaction opens with a
one-byte command — 0x01 DATAWRITE (host→PN532 frame), 0x02 STATREAD (read a
one-byte status; bit0 set = a response frame is ready), 0x03 DATAREAD
(PN532→host frame). The interface is byte-wise LSB-first, which the nRF5340
SPIM does in hardware (SPI_TRANSFER_LSB), so buffers hold ordinary MSB-order
bytes here and the peripheral flips them on the wire.
Each command, status poll, and frame read is its own CS-cycled transaction
(the same shape as the Adafruit/ESPHome PN532 drivers). DATAREAD clocks its
command byte and the complete response through one contiguous SPIM transfer.
The chip re-presents the current frame on each DATAREAD, so reading more bytes
than a frame holds is harmless as long as CS is dropped between frames — with
one exception the caller enforces: the ACK read is kept short
(PN532_ACK_READ_LEN) because the response follows it immediately and a long
over-read would clock it away.
Readiness is polled with STATREAD unless irq-gpios is wired (active low =
frame ready), in which case a GPIO edge wakes the waiting thread.

**depends on** [`modules/woz_nfc/src/pn532_bus.h`](architecture/modules.woz_nfc.src/pn532_bus.h.md)

### [`modules/woz_nfc/src/pn532.c`](architecture/modules.woz_nfc.src/pn532.c.md)

PN532 host-protocol driver. See pn532.h. OS-free: no Zephyr headers, no
allocation, no sleeping — waiting is delegated to the bus wait_ready op.

**depends on** [`modules/woz_nfc/src/pn532.h`](architecture/modules.woz_nfc.src/pn532.h.md)

### [`modules/woz_nfc/src/pn532_apdu.c`](architecture/modules.woz_nfc.src/pn532_apdu.c.md)

@file pn532_apdu.c
PN532 APDU command planner: parse ISO 7816-4 APDU structure (Case 1-4, short/extended), emit
passthrough or fragmented transport frames, handle GetResponse for extended data retrieval.

**depends on** [`modules/woz_nfc/src/pn532_apdu.h`](architecture/modules.woz_nfc.src/pn532_apdu.h.md)

### [`modules/woz_nfc/src/transport_none.cpp`](architecture/modules.woz_nfc.src/transport_none.cpp.md)

WozNfc backend for boards with no NFC frontend: polling never starts and no
NFC session is ever created, so Send()/Terminate() are unreachable in a
correct run; Send() reports invalid state defensively.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](architecture/modules.woz_nfc.include.woz_nfc/transport.h.md)

### [`modules/woz_nfc/src/transport_rfal.cpp`](architecture/modules.woz_nfc.src/transport_rfal.cpp.md)

WozNfc backend forwarding to the add-on's ST25R/RFAL transport unchanged.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](architecture/modules.woz_nfc.include.woz_nfc/transport.h.md)

### [`modules/woz_nfc/src/pn532.h`](architecture/modules.woz_nfc.src/pn532.h.md)

NXP PN532 host-protocol driver: frame codec and the command subset needed by
the Aliro reader transport. Bus-agnostic and OS-free — all I/O goes through
injected bus operations, so the whole layer compiles and runs in the host
test suite against a scripted fake bus.
Protocol reference: NXP UM0701-02 (PN532 User Manual).

**used by** [`modules/woz_nfc/src/pn532.c`](architecture/modules.woz_nfc.src/pn532.c.md), [`modules/woz_nfc/src/pn532_bus.h`](architecture/modules.woz_nfc.src/pn532_bus.h.md), [`modules/woz_nfc/src/transport_pn532.cpp`](architecture/modules.woz_nfc.src/transport_pn532.cpp.md)

### [`modules/woz_nfc/src/pn532_apdu.h`](architecture/modules.woz_nfc.src/pn532_apdu.h.md)

PN532-specific ISO 7816 APDU adaptation.
The Aliro stack (including the prebuilt library) negotiates sizes with the
User Device, but has no API for the reader controller's smaller local limit.
This adapter keeps that hardware constraint at the transport boundary.

**used by** [`modules/woz_nfc/src/pn532_apdu.c`](architecture/modules.woz_nfc.src/pn532_apdu.c.md), [`modules/woz_nfc/src/transport_pn532.cpp`](architecture/modules.woz_nfc.src/transport_pn532.cpp.md)

### [`modules/woz_nfc/src/pn532_bus.h`](architecture/modules.woz_nfc.src/pn532_bus.h.md)

Bus binding for the PN532 driver. One implementation is compiled in per
build (currently SPI: pn532_bus_spi.c). The transport uses only these
neutral names, so swapping the physical bus never touches pn532.c or
transport_pn532.cpp.

**depends on** [`modules/woz_nfc/src/pn532.h`](architecture/modules.woz_nfc.src/pn532.h.md)  ·  **used by** [`modules/woz_nfc/src/pn532_bus_spi.c`](architecture/modules.woz_nfc.src/pn532_bus_spi.c.md), [`modules/woz_nfc/src/transport_pn532.cpp`](architecture/modules.woz_nfc.src/transport_pn532.cpp.md)

## `ports/esp32/components/piv_ccid/`

### [`ports/esp32/components/piv_ccid/piv_ccid_usb.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid_usb.c.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid.h.md), [`ports/esp32/components/piv_ccid/include/piv_ccid_usb.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid_usb.h.md), [`ports/esp32/components/piv_ccid/include/piv_identity.h`](architecture/ports.esp32.components.piv_ccid.include/piv_identity.h.md)

### [`ports/esp32/components/piv_ccid/piv_identity.c`](architecture/ports.esp32.components.piv_ccid/piv_identity.c.md)

*No module docstring. First commit: "piv: gate macOS unlock on fresh presence".*

**depends on** [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md), [`ports/esp32/components/piv_ccid/include/piv_identity.h`](architecture/ports.esp32.components.piv_ccid.include/piv_identity.h.md)

### [`ports/esp32/components/piv_ccid/piv_ccid.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid.c.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](architecture/ports.esp32.components.piv_ccid.include/piv_apdu.h.md), [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid.h.md)

### [`ports/esp32/components/piv_ccid/piv_apdu.c`](architecture/ports.esp32.components.piv_ccid/piv_apdu.c.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](architecture/ports.esp32.components.piv_ccid.include/piv_apdu.h.md)

## `modules/woz_uwb/src/fira/`

### [`modules/woz_uwb/src/fira/fira_session.c`](architecture/modules.woz_uwb.src.fira/fira_session.c.md)

@file fira_session.c — Range + URSK store for the CCC Pre-POLL responder.

**depends on** [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/ccc/aliro_kdf.h`](architecture/modules.woz_uwb.src.ccc/aliro_kdf.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

### [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

@file fira_session.h — Range + URSK store for the CCC Pre-POLL responder.

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md), [`modules/woz_uwb/src/fira/fira_session.c`](architecture/modules.woz_uwb.src.fira/fira_session.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/fira/fira_device_config.h`](architecture/modules.woz_uwb.src.fira/fira_device_config.h.md)

@file fira_device_config.h — FiRa DS-TWR device/session parameter bag consumed by
fira_session.c.

## `integration/homeassistant/custom_components/openaliro/`

### [`integration/homeassistant/custom_components/openaliro/__init__.py`](architecture/integration.homeassistant.custom_components.openaliro/__init__.md)

HA=1-only OpenAliro direct-serial integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](architecture/integration.homeassistant.custom_components.openaliro/runtime.md)

### [`integration/homeassistant/custom_components/openaliro/diagnostics.py`](architecture/integration.homeassistant.custom_components.openaliro/diagnostics.md)

Redacted diagnostics for the HA=1 OpenAliro direct integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](architecture/integration.homeassistant.custom_components.openaliro/runtime.md)

### [`integration/homeassistant/custom_components/openaliro/event.py`](architecture/integration.homeassistant.custom_components.openaliro/event.md)

Access outcome event entity for the HA=1 OpenAliro direct integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](architecture/integration.homeassistant.custom_components.openaliro/runtime.md)

### [`integration/homeassistant/custom_components/openaliro/sensor.py`](architecture/integration.homeassistant.custom_components.openaliro/sensor.md)

Distance sensor for the HA=1 OpenAliro direct integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](architecture/integration.homeassistant.custom_components.openaliro/runtime.md)

### [`integration/homeassistant/custom_components/openaliro/config_flow.py`](architecture/integration.homeassistant.custom_components.openaliro/config_flow.md)

Manual direct-serial config flow for the HA=1 OpenAliro beta.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md)

### [`integration/homeassistant/custom_components/openaliro/device_trigger.py`](architecture/integration.homeassistant.custom_components.openaliro/device_trigger.md)

Granted and denied device-automation triggers for OpenAliro access events.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md)

### [`integration/homeassistant/custom_components/openaliro/const.py`](architecture/integration.homeassistant.custom_components.openaliro/const.md)

Constants for the HA=1 OpenAliro custom integration.

**used by** [`integration/homeassistant/custom_components/openaliro/__init__.py`](architecture/integration.homeassistant.custom_components.openaliro/__init__.md), [`integration/homeassistant/custom_components/openaliro/config_flow.py`](architecture/integration.homeassistant.custom_components.openaliro/config_flow.md), [`integration/homeassistant/custom_components/openaliro/device_trigger.py`](architecture/integration.homeassistant.custom_components.openaliro/device_trigger.md), [`integration/homeassistant/custom_components/openaliro/diagnostics.py`](architecture/integration.homeassistant.custom_components.openaliro/diagnostics.md), [`integration/homeassistant/custom_components/openaliro/event.py`](architecture/integration.homeassistant.custom_components.openaliro/event.md), [`integration/homeassistant/custom_components/openaliro/sensor.py`](architecture/integration.homeassistant.custom_components.openaliro/sensor.md)

### [`integration/homeassistant/custom_components/openaliro/runtime.py`](architecture/integration.homeassistant.custom_components.openaliro/runtime.md)

Home Assistant runtime bridge over the shared OpenAliro serial session.

**exposes** `OpenAliroRuntime`  ·  **used by** [`integration/homeassistant/custom_components/openaliro/__init__.py`](architecture/integration.homeassistant.custom_components.openaliro/__init__.md), [`integration/homeassistant/custom_components/openaliro/diagnostics.py`](architecture/integration.homeassistant.custom_components.openaliro/diagnostics.md), [`integration/homeassistant/custom_components/openaliro/event.py`](architecture/integration.homeassistant.custom_components.openaliro/event.md), [`integration/homeassistant/custom_components/openaliro/sensor.py`](architecture/integration.homeassistant.custom_components.openaliro/sensor.md)

## `modules/woz_aliro_stack/src/protocol/`

### [`modules/woz_aliro_stack/src/protocol/ble_message.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.c.md)

@file ble_message.c
BLE protocol message framing: parse and build protocol/message_id headers and payloads; parse and
extract Initiate Access, UWB control, Access Completed, and Reader Status Changed messages.

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/ble_timeout.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.c.md)

@file ble_timeout.c
Aliro BLE timeout supervisor (state machine + reply validator). Core: classify_attribute parses
BLE message type from attribute ID/length; is_allowed_reply maps request→reply types (including
Busy/GeneralError for any); has_response_timeout marks messages that start a timeout window;
collision_replaces_pending resolves priority when incoming messages arrive before the previous
one completes; set_pending / clear_pending manage state transitions. Designed to prevent timeouts
when the phone is responsive and terminate when not.

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.c.md)

@file nfc_auth.c
NFC Aliro protocol command builders: AUTH0 and AUTH1 APDU encoding, authentication data
construction, and response parsing for credential exchange and signature verification over NFC.

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md)

@file nfc_select.c
NFC SELECT command builder and response parser for Aliro. build_select_command emits 00 A4 04 00
09 `AID` 00. parse_proprietary_information decodes type-0x80 data from a SELECT response,
extracting protocol version (expedited phase only) and extended-length sizes (0x7f66 TLV).
parse_select_response and parse_select_response_ex validate the trailing 9000, check AID, and
call parse_proprietary_information.

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md)

@file nfc_step_up.c
NFC step-up messaging: compact-key CBOR encoder/decoder for Aliro DeviceRequest and SessionData
(ISO 18013-5). Core: put appends to writer buffer; cbor_head / cbor_bytes / text build encoded
items; cbor_read_head parses with validation (non-minimal representation rejected);
build_device_request constructs DeviceRequest (compact keys); wrap_session_data /
unwrap_session_data encode/decode SessionData; wrap_do53 / unwrap_do53 TLV-wrap messages;
build_envelope_command / build_get_response_command and collect_response chain ISO APDU commands.

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/access_document.c`](architecture/modules.woz_aliro_stack.src.protocol/access_document.c.md)

@file access_document.c
Compact-key CBOR parser for Aliro Access Documents (compact subset of ISO 18013-5 mDoc). Parses
strictly with iterative depth traversal (no stack recursion), validates CBOR encoding (no floats,
no simple values with payloads, minimal representation), and enforces a 25-level nesting bound.
Core: parse_at walks encoded items; root validates full-buffer consumption; child_at / map_find_*
retrieve nested elements; integer / timestamp extract scalar fields.

**depends on** [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md)

### [`modules/woz_aliro_stack/src/protocol/tlv.c`](architecture/modules.woz_aliro_stack.src.protocol/tlv.c.md)

@file tlv.c
BER-TLV parser and encoder for Aliro protocol: parse TLVs with definite length and advance
offset, compute encoded sizes, and write new TLVs.

**depends on** [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md)

@file access_document.h
Aliro access document parsed from CBOR and COSE_Sign1 envelope: device public key, issued data
element, issuer-signed item, signature, issuer key ID and certificate, validity period, and
optional iteration count.

**used by** [`modules/woz_aliro_stack/src/protocol/access_document.c`](architecture/modules.woz_aliro_stack.src.protocol/access_document.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md)

Aliro 1.0 Bluetooth LE message framing (section 11.7).

**used by** [`modules/woz_aliro_stack/src/protocol/ble_message.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.c.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.h.md)

Aliro 1.0 Bluetooth LE responseTimeout rules (section 11.9).

**used by** [`modules/woz_aliro_stack/src/protocol/ble_timeout.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.h.md)

Aliro 1.0 expedited authentication APDU codecs.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.h.md)

@file nfc_select.h
Parsed result of an NFC SELECT command for the Aliro applet: negotiated protocol version, maximum
command and response data lengths (from TLV or default), extended-length support, and the raw
proprietary information TLV (A5 tag) for further parsing.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md)

Aliro 1.0 / ISO 18013-5 NFC step-up message and APDU codecs.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

Minimal strict BER/DER-TLV reader for Aliro APDU payloads.

**used by** [`modules/woz_aliro_stack/src/protocol/ble_message.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md), [`modules/woz_aliro_stack/src/protocol/tlv.c`](architecture/modules.woz_aliro_stack.src.protocol/tlv.c.md)

## `ports/esp32/apps/reader/main/`

### [`ports/esp32/apps/reader/main/app_shell.c`](architecture/ports.esp32.apps.reader.main/app_shell.c.md)

ESP32-IDF console shell for the standalone Aliro UWB responder bench app: registers status, range, aliro-start/stop, provisioning, trust, and clear commands and runs the linenoise-based REPL.

**depends on** [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md), [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md)

### [`ports/esp32/apps/reader/main/main.c`](architecture/ports.esp32.apps.reader.main/main.c.md)

Woz UWB ranging engine on ESP32-S3 (ESP-IDF) — minimal bring-up app.
Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
polls for a range. With no iPhone/initiator present this proves the SPI +
DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
The demo responder lifecycle + interactive console live in app_shell.c.

**depends on** [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md), [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md)

### [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md)

**used by** [`ports/esp32/apps/reader/main/app_shell.c`](architecture/ports.esp32.apps.reader.main/app_shell.c.md), [`ports/esp32/apps/reader/main/main.c`](architecture/ports.esp32.apps.reader.main/main.c.md)

## `integration/homeassistant/`

### [`integration/homeassistant/aliro_mqtt_bridge.py`](architecture/integration.homeassistant/aliro_mqtt_bridge.md)

Republish the lock's console log to MQTT as Home Assistant entities.

Usage: aliro_mqtt_bridge.py --port /dev/tty.usbmodem1234 [--broker HOST] [--node NAME]
       aliro_mqtt_bridge.py --port - --dry-run < captured.log

Reads the UWB console line by line, extracts the per-block range line and the
access verdict, and publishes them as two MQTT Discovery entities: a distance
sensor in millimetres and an access event carrying granted/denied. Lines
matching neither pattern are ignored.

The range line is gated on the firmware side behind CONFIG_WOZ_PRETTY_SHELL and
uwb_rxdiag_rng_get(), so it only appears once `aliro frames on` has been issued
on the shell. Without that, the access events still flow but distance stays
unpublished.

Reading from '-' takes the log on stdin, which with --dry-run exercises the
parser and the payloads without a broker or a board attached. paho-mqtt is
imported only when publishing, pyserial only for a real port, so neither is
needed for a dry run.

**depends on** [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md)

## `ports/esp32/apps/matter-lock/main/lock/`

### [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.cpp.md)

AliroReaderDelegate: implements the Aliro reader-provisioning and BLE-UWB portions of the Matter
DoorLock::Delegate interface, backing the controller-facing GetAliro*/SetAliroReaderConfig
commands and persisting the provisioned reader identity via aliro_reader_provision_identity.
Bridges Matter cluster commands to the underlying aliro_reader NVS-backed identity/trust store
and to the BLE advertising layer (refreshed when the group resolving key changes).

**depends on** [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.h.md)

### [`ports/esp32/apps/matter-lock/main/lock/door_lock_callbacks.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_callbacks.cpp.md)

Matter DoorLock cluster plugin callbacks: wires the ESP32 port's BoltLockManager into the
Matter DoorLock cluster's lock/unlock commands, user and credential storage, schedule
storage, cluster init, and auto-relock notification hooks.

**depends on** [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)

### [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.cpp.md)

BoltLockManager: Matter door lock cluster backing store for the ESP32 port. Implements the DoorLock cluster's user, credential, and weekday/yearday/holiday schedule get/set callbacks over fixed-size in-memory tables mirrored to NVM (ESP32Config blobs), plus lock/unlock actuation and PIN validation. Cluster indices are one-indexed by Matter and decremented internally before bounds-checking against this platform's fixed capacity limits.

**depends on** [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)

### [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.h.md)

Declares AliroReaderDelegate, the Aliro (Apple Home Key) reader-provisioning and BLE-UWB half of
the Matter DoorLock cluster delegate, bridging controller commands to the on-device reader
identity, trust store, and BLE advertising state.

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.cpp.md)

### [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)

Door lock manager for the Matter DoorLock cluster: owns bolt lock state plus the users,
credentials, and weekday/yearday/holiday schedules backing the cluster's server attributes.
Declares BoltLockManager (accessed via the BoltLockMgr() singleton) and the
LockInitParams::LockParam/ParamBuilder types used to configure it from zap-derived capacity
attributes at init time.

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_callbacks.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_callbacks.cpp.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.cpp.md)

## `ports/esp32/components/aliro_reader/`

### [`ports/esp32/components/aliro_reader/presence_link.c`](architecture/ports.esp32.components.aliro_reader/presence_link.c.md)

Presence dongle commands (see presence_link.h). `prove` ends every old Aliro
link, waits for a new trusted credential authentication and a later trusted
UWB range, then signs that post-challenge result under a persistent P-256 key.
These live on the ordinary console rather than a private binary channel, so one
board can be provisioned (aliro-import) and queried for presence without
reflashing between modes, and so a stray log line is just another line instead of
a corrupted frame.

**depends on** [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md)

### [`ports/esp32/components/aliro_reader/presence_link.h`](architecture/ports.esp32.components.aliro_reader/presence_link.h.md)

Presence dongle commands (CONFIG_WOZ_PRESENCE): fresh, challenge-driven signed
statements from a new trusted Aliro authentication and later UWB range, turning
proximity of a provisioned iPhone into a factor any tool can check. See
tools/presence_verify.py and tools/presence_git.py for the other end.
These are console commands rather than a private binary channel, so the shell
stays available on the same board: provisioning (aliro-import) and presence both
work without reflashing between modes. Every response is one tagged hex line, so
a log line landing mid-conversation is just another line rather than corruption:
presence pub                 -> PRESENCE-PUB <65 bytes hex>   (enrolment)
presence credential          -> PRESENCE-CRED <8 bytes hex>   (pinned human)
presence prove <nonce-hex>   -> PRESENCE-P256 <115 bytes hex> (fresh proof)
anything rejected            -> PRESENCE-ERR <reason>

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md), [`ports/esp32/apps/reader/main/app_shell.c`](architecture/ports.esp32.apps.reader.main/app_shell.c.md), [`ports/esp32/apps/reader/main/main.c`](architecture/ports.esp32.apps.reader.main/main.c.md), [`ports/esp32/components/aliro_reader/presence_link.c`](architecture/ports.esp32.components.aliro_reader/presence_link.c.md), [`ports/esp32/components/piv_ccid/piv_identity.c`](architecture/ports.esp32.components.piv_ccid/piv_identity.c.md)

### [`ports/esp32/components/aliro_reader/aliro_prov_nvs.c`](architecture/ports.esp32.components.aliro_reader/aliro_prov_nvs.c.md)

NVS-backed persistence for Aliro reader provisioning: loads and stores the serialized reader
identity and trust store built by aliro_prov.c.
Lazily initializes NVS on first use; safe to call alongside aliro_ble's own nvs_flash_init.

### [`ports/esp32/components/aliro_reader/aliro_stepup_worker.c`](architecture/ports.esp32.components.aliro_reader/aliro_stepup_worker.c.md)

@file aliro_stepup_worker.c
Step-up document verification worker for ESP32. Runs on a dedicated FreeRTOS task (6 KB stack, priority 4). Lazily creates a single-slot queue on first submission. Non-blocking submission: if a previous job is still enqueued, the new job is dropped. Verdict and connection handle are stored in shared state (spinlock-protected) and retrieved via aliro_stepup_worker_last(). Logging includes decrypted DeviceResponse hex and verdict breakdown (validity, element count, issuer found, signature OK, doctype OK, time OK, iteration OK).

## `ports/esp32/components/woz_uwb/port/`

### [`ports/esp32/components/woz_uwb/port/dw3000_hw.c`](architecture/ports.esp32.components.woz_uwb.port/dw3000_hw.c.md)

ESP-IDF GPIO/IRQ backend for the DW3000 decadriver — implements dw3000_hw.h.
Replaces the Zephyr deps/dw3000/platform/dw3000_hw.c (not compiled here).
IRQ mirrors the Zephyr design: the GPIO ISR wakes a dedicated high-priority
task (pinned to core 1) that calls dwt_isr() while the IRQ line stays high —
dwt_isr does SPI, so it cannot run in true ISR context. Also provides the
cycle-counter diag symbols that dwt_uwb_driver/dw3000/dw3000_device.c
references (Xtensa CCOUNT via esp_cpu_get_cycle_count).

**depends on** [`ports/esp32/components/woz_uwb/port/board_pins.h`](architecture/ports.esp32.components.woz_uwb.port/board_pins.h.md)

### [`ports/esp32/components/woz_uwb/port/dw3000_spi.c`](architecture/ports.esp32.components.woz_uwb.port/dw3000_spi.c.md)

ESP-IDF SPI backend for the DW3000 decadriver — implements dw3000_spi.h.
Replaces the Zephyr deps/dw3000/platform/dw3000_spi.c (not compiled here).
CS is a plain GPIO (spics_io_num = -1), matching the Zephyr cs-gpios model, so
the wakeup path can hold CS low ~500us. Each DW3000 command is one CS-low
full-duplex transfer: header + body assembled in a DMA-capable, word-aligned
bounce buffer; on reads the body slice of the RX buffer is copied back.

**depends on** [`ports/esp32/components/woz_uwb/port/board_pins.h`](architecture/ports.esp32.components.woz_uwb.port/board_pins.h.md)

### [`ports/esp32/components/woz_uwb/port/board_pins.h`](architecture/ports.esp32.components.woz_uwb.port/board_pins.h.md)

DW3000 (DWM3000EVB) wiring per ESP32 target, SPI2/FSPI. Source of truth for
the wiring table in docs/esp32-bringup.md. Change to match how the DWM3000EVB
is soldered to your board.

**used by** [`ports/esp32/components/woz_uwb/port/dw3000_hw.c`](architecture/ports.esp32.components.woz_uwb.port/dw3000_hw.c.md), [`ports/esp32/components/woz_uwb/port/dw3000_spi.c`](architecture/ports.esp32.components.woz_uwb.port/dw3000_spi.c.md)

### [`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c`](architecture/ports.esp32.components.woz_uwb.port/woz_wrap_stubs.c.md)

Minimal ESP-IDF port of the essential RX-callback shim.
The Nordic build routes DW3000 RX events through uwb_rxdiag.c's
__wrap_dwt_setcallbacks -> shim_rxok, which (after the blob's own
prepoll_rx_rearm arms the SP3 POLL window) calls ccc_shim_rx_try_prepoll to
decrypt+warm the NEXT block's STS.  That bootstrap warm is what flips
g_warm_valid true so the POLL window ever gets armed and Response_0 sent.
This port omits uwb_rxdiag.c wholesale (its heartbeat needs Zephyr k_work,
which the compat layer does not provide), so without this shim dwt_setcallbacks
installs prepoll_rx_rearm directly, ccc_shim_rx_try_prepoll is never reached,
g_warm_valid stays false, and the responder receives Pre-POLLs but never
replies.  Re-create only the essential chain here (no k_work, no diagnostics).
Also keeps the dwt_configurestsmode pass-through the essential RX path needs.

## `tools/`

### [`tools/aliro_gait.py`](architecture/tools/aliro_gait.md)

Aliro Gait: carry-motion features from Aliro Lab walk-up captures.

Usage: python3 tools/aliro_gait.py [-o report.html] [label=]capture.log ...

E1 probe of the passive carry verification experiment: for every walk-up
transaction in the given "[ALAB]" captures, detrend the per-block
trusted-range series, FFT the residual, and report the carry-motion features
(cadence, stride regularity, approach speed, deceleration, closest approach,
residual RMS) plus a per-window carried/stationary verdict. With two or more
labels (one per carrier, e.g. alice=alice.log bob=bob.log) it also runs
leave-one-out nearest-centroid classification to measure whether the features
separate the carriers — the pre-registered Tier-2 GO bar is >= 80%.

The block duration (and the phone's implied RAN multiplier) is derived from
the range timestamps themselves, so no extra firmware logging is needed.
Exit status: 0 = report produced, 2 = usage/input error.

**depends on** [`tools/aliro_lab.py`](architecture/tools/aliro_lab.md)

### [`tools/aliro_lab.py`](architecture/tools/aliro_lab.md)

Aliro Lab: score a captured reader serial log.

Usage: python3 tools/aliro_lab.py [--cir <taps.csv>] <capture.log> [report.html]

Parses the structured "[ALAB] t=<us> ev=..." trace lines the firmware emits
when CONFIG_WOZ_ALIRO_LAB is enabled (see modules/woz_aliro/src/aliro_lab.h),
groups them into walk-up transactions, and reports phase timings, the flow
taken (fast vs standard), and pass/warn/fail invariant checks — to the
terminal and as a self-contained HTML report (default: <capture.log>.html).

With --cir, the windowed-CIR taps (ev=uwb.cir, channel-impulse Stage 1) are
also written to a CSV (t_us,n,i,re,im,mag2) for offline inside/outside
labeling and analysis; the scoring/report output is unchanged.

Every check encodes an invariant of this repo's reader implementation (see
internal notes in the check text), nothing else. Exit status: 0 = no failing
check, 1 = at least one FAIL, 2 = usage/input error.

**used by** [`tools/aliro_gait.py`](architecture/tools/aliro_gait.md)

### [`tools/presence_git.py`](architecture/tools/presence_git.md)

Presence-signed git tags: prove a human was physically present at a release.

A GPG signature proves WHO made a tag. It does not prove they were there: a
stolen key, a compromised CI runner or a coerced automated pipeline all produce
perfectly valid signatures. This adds the orthogonal claim -- that a provisioned
credential was physically within a few tens of centimetres of the machine when
the tag was made -- measured by UWB time-of-flight. The protocol structure
resists a simple relay shortening distance, but this project's relay resistance
has not been experimentally measured.

    presence_git.py enroll --port /dev/tty.usbmodem... --name my-dongle
    presence_git.py sign   --tag presence/1.2.0 --port /dev/tty.usbmodem...
    presence_git.py verify --tag presence/1.2.0          # what CI runs

HOW THE NONCE WORKS, AND WHAT THAT COSTS
Everywhere else in this protocol the verifier mints a random nonce and remembers
it, which makes replay impossible. CI cannot do that: it was not present when
the tag was made, so it has no nonce to remember. The nonce is therefore DERIVED
from what is being signed:

    nonce = SHA-256("openaliro-presence-tag-v1\0" + tag + "\0" + commit)[:16]

so any verifier can recompute it. The assertion is then cryptographically bound
to exactly one (tag name, commit) pair and is worthless for any other.

The honest cost, which must not be buried: a derived nonce binds the proof to an
ARTEFACT, not to a MOMENT. The dongle has no trusted wall clock and reports
unix_ms = TIME_NONE, so a verifier cannot tell whether the presence happened
today or last year. Someone who obtained an assertion for this exact (tag,
commit) pair before it was published could publish that tag themselves without
being present. That window is narrow -- it requires the assertion before the tag
exists -- but it is real. It closes the day the dongle carries attested time,
which is precisely why unix_ms is a separate field in the wire format rather
than something derived from uptime.

Enrolled keys and allowed credential ids live in .presence/enrolled, committed
to the repo so both the trusted dongle and named human are reviewable in history
like any other change. The tag carries only a key id; policy always comes from
that file, because a tag that carried its own keys would authorize itself.

Stdlib only, except that enroll/sign import pyserial lazily to talk to a real
dongle. verify -- the half CI runs -- needs no serial port and no extra package.

**depends on** [`tools/presence_verify.py`](architecture/tools/presence_verify.md), [`tools/tui/src/serial.ts`](architecture/tools.tui.src/serial.ts.md)  ·  **used by** [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md)

### [`tools/presence_verify.py`](architecture/tools/presence_verify.md)

Verify an ECDSA-P256 presence assertion against a dongle's public key.

This is the portable half of the presence primitive. A P-256 assertion is
checkable by any holder of the dongle's public point, which is what lets a CI
job, a release-tag hook or a second reviewer accept "a human was physically at
that machine" without being trusted with a secret. A shared-secret assertion
could only be checked by a holder of the key that could equally forge it, which
is why that mode was retired rather than kept alongside this one.

Stdlib only. The curve arithmetic is delegated to the openssl binary already
present on any machine that can run a CI job, so nothing is vendored and no
third-party Python package is required. That also keeps this file honest about
what it does NOT do: it never implements P-256 itself.

The wire format is defined by modules/woz_aliro/src/aliro_assert.c, and the
verdicts and their ORDER mirror aliro_assert_verify_p256() exactly, so the two
implementations cannot disagree about what a frame means. The offsets below are
drift-gated against that C source by tests/host/test_presence_verify.py.

A distance is only worth as much as the measurement behind it, so the frame also
carries that measurement's integrity evidence and this verifier refuses a frame
that does not claim a well-correlated STS. Rejecting is the whole point: an
undefended 19 cm and a defended 19 cm arrive as the same integer, so a verifier
that ignores the evidence cannot tell a measurement from an assertion.

Usage:
    presence_verify.py --pubkey <hex|file> --frame <hex|file> --nonce <hex>
                       --cred-id <hex> [--max-cm N] [--min-uptime-ms N]
                       [--min-sts-quality N] [--json]

Exit status is 0 only when presence is confirmed, so it can gate a command
directly. Every rejection exits 1 and names its reason.

**used by** [`host/presence/presence_service.py`](architecture/host.presence/presence_service.md), [`tools/presence_git.py`](architecture/tools/presence_git.md)

### [`tools/aliro.lua`](architecture/tools/aliro.lua.md)

*No module docstring. First commit: "Add Wireshark dissector for the clear-text Aliro BLE plane".*

### [`tools/docs_3d.py`](architecture/tools/docs_3d.md)

Render the whole code surface as a flyable 3D graph: site/graph3d.html.

The architecture page's 2D graphs show the curated module clusters. This pass
builds the immersive counterpart over the entire tree — every source file the
docs cover, from the reader engine to the flash scripts — as a 3D force graph
you can orbit, filter and fly through. Clicking a file opens a panel with its
description, its imports both ways, and its API symbols, each linking into the
reference tree.

Everything is mined from what the repo already publishes, no extra analysis:

  * docs/architecture/<group>/<file>.md — one node per page: the H1 carries
    the source path, the first paragraph the description, the "**depends on**"
    row the outgoing edges. Reversing those edges gives "used by".
  * site/nav.js — the search index built earlier in the pipeline; its
    function/class/macro entries, keyed by page slug, become each node's
    symbol list with working anchors.
  * architecture.html's gv-slots marker — the cluster -> color slot map the
    2D graphs persisted, so both views and the sidebar dots stay color-keyed
    alike; directories beyond the curated clusters get slots from an extended
    palette.

The renderer is the 3d-force-graph bundle (MIT), vendored under
internal/vendor/ (gitignored) and copied into site/; when the vendor copy is
missing it is fetched once from unpkg. Offline with no vendor copy, the pass
skips cleanly: no page, no entry button, the 2D graphs stand alone.

The stage is always dark — same rule as the site's code panels — while the
page chrome follows the reader's theme (dm-theme, then the OS preference).

Run from the repo root, after the reference fill (the symbol index must be
complete) and before the link pass (which validates the links minted here).

### [`tools/docs_api.py`](architecture/tools/docs_api.md)

Fill the reference pages the page generator leaves bare.

The generator documents code where it is defined: functions with bodies,
structs, inline helpers. A header that only *declares* things — prototypes,
macros, enums — renders as a hero line and a "used by" row, which reads as
an empty page even when every declaration in the file carries a doc comment.

This pass parses those headers straight from the working tree and appends
the missing declarations in the generator's own api-entry markup, so the
"On this page" rail and the search palette treat them like any other entry:

  * function prototypes (with their /** brief */ if present),
  * documented #defines, plus undocumented value-carrying ones — a pin map
    is worth listing even uncommented; include guards are not,
  * enum/struct/union declarations the page does not already show.

Anything the page already renders is skipped by anchor id, so running after
the generator adds only what it left out. New entries are also appended to
the search index in nav.js. Run from the repo root, after docs_graph.py and
before the link pass.

### [`tools/docs_cmds.py`](architecture/tools/docs_cmds.md)

Render runnable command blocks as one copy chip per command.

A guide's bash block renders as a plain <pre>: the trailing `# comment` sits
in the same monospace run as the command, and the block-level copy button
copies comments and all. For a block of commands the reader wants the
opposite: each command on its own row, the comment visibly muted, and a Copy
button that yields exactly the command — the chip treatment the landing
page's quick start already uses. The chip CSS and the .js-copycmd handler
ship on every page, so the rewrite is markup only.

Only blocks that are unambiguously command sequences are touched: every
non-blank line must start with an allowlisted command (optionally prefixed
with VAR=value assignments). Device logs, pseudocode and C fragments never
match and render as before.

Run from the repo root, after docs_nav.py and before the link pass.

### [`tools/docs_flash.py`](architecture/tools/docs_flash.md)

Publish the browser flasher: site/flash/ = the web-flasher/ page + firmware.

The page and its ESP Web Tools manifest are committed in web-flasher/; the
merged firmware image is not. GitHub release assets are served without CORS
headers (probed 2026-07-22: neither the github.com redirect nor its CDN
answers Access-Control-Allow-Origin), so the browser cannot fetch the image
from the release; it has to sit next to the page on the same origin. This
pass stages it at site-build time, preferring in order:

  1. web-flasher/openaliro-matter-lock-esp32s3.bin (gitignored): a local
     `idf.py merge-bin` output for bench runs, published with the committed
     manifest (version "dev").
  2. The latest release's loose assets (openaliro-matter-lock-esp32s3.bin +
     openaliro-matter-lock.manifest.json, uploaded by release.yml), fetched
     server side where CORS does not apply; the manifest arrives already
     version-stamped.
  3. Neither: skip the page entirely, loudly. An Install button whose
     firmware 404s is worse than no page, and before the first release this
     is the normal state of a fresh checkout.

When the page is staged, the site links to it: a row in the get-started hub's
Hardware bucket and a one-line lead under the landing page's "Get running"
heading. Injected here and not in the sources on purpose — the flash page
only exists when firmware was found, and a committed link would 404 on every
checkout without a release. No firmware, no links, nothing dangles.

Run from the repo root, after the link pass: the page is standalone and its
links are absolute or flash-local, so it needs no rewriting. docs.sh drives it.

### [`tools/docs_github.py`](architecture/tools/docs_github.md)

Point the rendered site back at its GitHub repository.

The page generator renders prose, navigation and reference pages; it does not
know where the source lives. This pass adds that context, with the repository
URL derived from the origin remote at build time, never hardcoded:

  * every top-level page gets a repository chip in the top bar: the GitHub
    mark, the owner/repo name, and live star/fork counts fetched client-side
    from the GitHub API (cached in localStorage for an hour; the counts stay
    hidden if the API is unreachable, the link still works).
  * the landing page's hero gets a GitHub button next to the existing calls
    to action, and a "Get running" section under the demo figure: clone to
    flashed board in five copyable steps, mirroring the README quickstart.

Idempotent for the same reason docs_media.py is: when the page generator is
not configured, the earlier passes run over a site/ kept from a previous
build, so a page may already carry the injections. Run from the repo root,
after docs_media.py and before the link pass.

### [`tools/docs_graph.py`](architecture/tools/docs_graph.md)

Make the architecture page's dependency graph legible.

The page generator emits the module import graph as one flat flowchart and a
zoomable shell around it. At this repo's size that renders as an unreadable
crop: dozens of modules, self-loop artifacts (a module importing its own
header), and a natural width several times the shell's, so the default 1:1
view shows two boxes and a tangle of splines.

This pass restructures the presentation, deriving everything from the page
itself so nothing is hand-curated to drift:

  * self-loop edges are dropped — at module level they are import artifacts,
    not information.
  * each module is assigned to its source directory, read from the page's own
    per-module headings, and the flat graph becomes clustered subgraphs.
  * a subsystem-level overview graph — one node per directory cluster, one
    arrow per aggregated dependency — goes above it. Small enough to be
    crisp at natural size, it answers the layering question at a glance.
  * every page with diagrams gets two script shims around the generator's
    nav.js: one tightens mermaid's layout spacing and bumps its font before
    the first render, the other clicks each diagram's own Fit control when
    the rendered graph overflows its shell, so big graphs open showing their
    whole shape instead of a random crop — and makes the shells direct:
    drag pans, cmd/ctrl+scroll (and trackpad pinch) zooms around the
    cursor, and plain or shift+scroll stays native, so the shell scrolls
    vertically or horizontally like any scrollable pane.
  * the per-module sections lose their visual noise: headings show the file
    name with the directory as a small eyebrow above it instead of one long
    path, the "depends on" rows become compact base-name chips (full path
    on hover) instead of comma-separated full paths, and the blurbs drop
    the "@file <name> — " prefix that would repeat the heading above them.
    The chip and prefix tidy also runs on every module reference page,
    whose "used by" rows and hero blurbs carry the same noise.
  * then the whole flat run of sections folds into one collapsed drill-down
    per directory cluster — color-dotted to match the graphs, a compact
    link row per module — so the page ends at a screenful instead of a
    hundred sections.
  * every graph gets a full-screen control: the wrap pins over the viewport
    with the same drag/zoom behavior, and Esc or the button collapses it.
  * a sitewide sidebar shim regroups the flat guide list under the same
    topic captions the landing page derives, and marks each reference
    directory group with its cluster's color dot from the graphs.

Idempotent for the same reason docs_media.py is: when the page generator is
not configured, the earlier passes run over a site/ kept from a previous
build, so a page may already carry the injections. Run from the repo root,
after docs_github.py and before the link pass.

### [`tools/docs_links.py`](architecture/tools/docs_links.md)

Repair cross-document links in the rendered site, then assert none are left broken.

The guide pages are authored as markdown that must also read correctly on GitHub,
so they link to other documents as `other.md` and to sources as `../modules/x.c`.
Neither form resolves once the pages are rendered into site/:

  * `other.md`            -> `other.html`, when that page was rendered
  * `../modules/x.c`      -> the file on GitHub, since sources are not published

Anything still unresolved after the rewrite is a genuine broken link and fails
the build. Run from the repo root, after both generators.

### [`tools/docs_media.py`](architecture/tools/docs_media.md)

Add the repo's imagery to the rendered site: demo screenshots and a share card.

The page generator renders prose, navigation and reference pages; it does not
publish images. This repo has three it wants on the site:

  * assets/grid-demo-light.webp / assets/grid-demo-dark.webp — one demo grid of
    the lock in use, in a light and a dark rendering (the README keeps its own,
    separate grid in assets/grid-demo.webp). Injected into the landing
    page as a figure that follows the site's theme: the toggle's `data-theme`
    attribute wins, the OS preference is the fallback — the same precedence
    the site's own stylesheet uses.
  * assets/social-preview.png — the share card. Every top-level page gets
    Open Graph / Twitter meta pointing at it, with an absolute URL derived
    from the origin remote (link scrapers ignore relative image URLs).

Idempotent on purpose: when the page generator is not configured, the earlier
passes run over a site/ kept from a previous build, so a page may already carry
the injections. Run from the repo root, after the generators and before the
link pass.

### [`tools/docs_nav.py`](architecture/tools/docs_nav.md)

Give the rendered site one curated reading order.

The generator ranks the guide list by keyword buckets, which is a reasonable
default and a poor journey: install and configure material was scattered, and
a reader finishing one page got no pointer to the next. This pass owns the
order in one place:

  * the landing page's Guides section is rebuilt into curated buckets
    (Set up first, deep dives after) — and because the sidebar shim mirrors
    the landing page's buckets, the sidebar follows automatically,
  * every page on the journey gets a prev/next pager, so there is always a
    next page and it is always the right one,
  * each guide's hero eyebrow names its bucket instead of the generic
    "Guide".

The buckets and the journey are the same list, so they cannot drift apart.
A guide added without a place in it fails the build here, on purpose: the
author decides where it belongs, or this pass would silently undo the point
of having a curated order.

Run from the repo root, after docs_start.py (start.html must exist to lead
the journey) and before docs_graph.py (whose sidebar shim reads the landing
page's buckets as rebuilt here).

### [`tools/docs_start.py`](architecture/tools/docs_start.md)

Give the rendered site a real "Get started" landing.

The hero's Get-started button used to deep-link straight into the ESP32
bring-up checklist — an fine first page for exactly one kind of reader.
This pass builds start.html instead: one landing that holds every track
(hardware, toolchain, build and test, firmware internals, protocol
research, project and CI), each a card that drills down in place to the
commands, installs and guides that track needs. The page is assembled from
an existing rendered guide page, so it always carries the current shell —
sidebar, palette, theme toggle and the other passes' injections.

Also part of wayfinding, on every page:

  * the sidebar gains a Get-started entry next to Overview,
  * the search button gets the visual weight a primary control deserves
    (accent tint, a couple of attention pings on load) and the palette a
    springier open — search is how readers actually move around, so it
    should not look like chrome.

Run from the repo root, after docs_github.py and before docs_graph.py, so
the page exists before the sitewide shims and the link pass run.

### [`tools/docs_theme.py`](architecture/tools/docs_theme.md)

Retheme the rendered site: warm paper surfaces, serif display headings.

The page generator ships a neutral blue-on-gray look. This pass restyles the
rendered output — never the generator — into the warm editorial style the
project wants: ivory paper backgrounds, near-black ink, a terracotta accent,
tan links in dark mode, and a serif display face over the headings. Two files
carry the whole theme:

  * site/style.css — every generated page links it, and every earlier pass
    styles its injections through the sheet's custom properties (--ground,
    --ink, --accent, …). Appending a redefinition of those properties at the
    end of the sheet wins the cascade everywhere at once, so the sidebar, the
    landing cards, the command chips and the search palette all follow without
    touching a single HTML file. A short component layer after the variables
    covers what variables cannot express: heading typefaces and the always-dark
    code panels.
  * site/api/doxygen-awesome.css — the reference tree's stylesheet exposes the
    same kind of seam (--page-background-color, --primary-color, …), so the
    API pages get the matching palette and headline face.

The display face is Source Serif 4 from Google Fonts, pulled with @import —
which CSS requires ahead of every rule, so the import is prepended while the
overrides are appended. Body text stays on the system sans stack.

Idempotent like the other passes: a marker comment guards both files, so
re-running over a kept site/ changes nothing. Run from the repo root, any time
after the generators; it edits only the two stylesheets, no page markup.

### [`tools/docs_title.py`](architecture/tools/docs_title.md)

Title the generated pages after the repository, not after the checkout directory.

Older page-generator releases took the project name from the basename of the
directory they ran in. In a linked worktree that name is the worktree
directory's, not the repository's, which put the wrong title on every page and
in the committed docs/ tree. The current release derives the name from git
itself, so this pass is a safety net that normally rewrites nothing.

The net only looks where a title can actually sit: the <title> tag, the
sidebar brand, and h1 headings in the rendered pages; the markdown H1 lines in
the two committed docs/ pages. It must not look anywhere else. A checkout can
be named after an ordinary word of the prose — a worktree named after, say,
the very thing this site is — and a blanket replacement would then rename that
word through running text, the generator's own ownership stamp (breaking its
regeneration), and the reference tree's rendered source listings. The
reference tree is excluded entirely: doxygen takes its project name from
docs/Doxyfile, never from the checkout.

The repository name comes from the common git directory, which every worktree
shares, so it is the same value from any checkout. When the two names agree
this is a no-op, which is the case in the main checkout.

Run from the repo root, after the generators and before the link pass.

### [`tools/docs_twin.py`](architecture/tools/docs_twin.md)

Fold the interactive walk-up digital twin into the rendered site.

The page generator renders prose, navigation and reference pages; it does not
know about the standalone twin. This repo ships one:

  * web-twin/index.html — a self-contained page (inline JS/CSS, no network) that
    drives the reader's real unlock decision logic as a visitor walks a phone up
    to a door. It is themed off the same tokens as the site, so it drops in as
    site/twin.html and reads as the same product.

This pass copies that page in and adds one call-to-action on the landing page,
linking to it, anchored on the same explore-card list docs_media uses. The link
pass that runs later then validates site/twin.html resolves.

Idempotent on purpose: when the page generator is not configured, earlier passes
run over a site/ kept from a previous build, so the landing page may already
carry the CTA. Run from the repo root, after the generators and before the link
pass.

### [`tools/flight_recorder.py`](architecture/tools/flight_recorder.md)

flight_recorder.py — carry a recorded UWB walk-up off the device and turn it
into replayable / fuzzable artifacts.

The firmware's `fr dump` console command hex-encodes its RAM ring as `[FREC]`
serial lines (see modules/woz_uwb/src/facade/flight_recorder.c). This tool:

  * reconstructs the binary trace from those lines (or reads a `.frc` directly),
  * prints a human summary of the recorded session,
  * extracts the received UWB frames into a fuzz corpus (seeding
    tests/host/fuzz with genuine RF sessions).

Only the frames (already on-air ciphertext) go to the corpus — never the CONFIG
record's URSK, so a shared corpus carries no session key material.

SECURITY: raw serial logs containing `[FREC]` records and binary `.frc` files
contain the CONFIG record's full ephemeral URSK. Keep them private and do not
attach them to public issues. Only the extracted frame corpus excludes the key.

Usage:
  flight_recorder.py <capture.log | trace.frc> [corpus_dir]

With a `.log` input the reconstructed trace is written next to it as `.frc`.
With a corpus_dir the frames are written there as `frame_NNNN.bin`. Stdlib only;
the binary format mirrors flight_recorder.h byte for byte.

### [`tools/piv_pin.py`](architecture/tools/piv_pin.md)

Provision or change the OpenAliro PIV PIN through macOS PC/SC.

### [`tools/power_profile.py`](architecture/tools/power_profile.md)

Power profile: turn a gated-walk-up serial log (+ optional power capture)
into the mA / unlock-latency / approach numbers of the RSSI-gate study.

Usage: python3 tools/power_profile.py <capture.log> [--ppk trace.csv]
                                      [--tag LABEL] [--shift SECONDS] [--csv out.csv]
       python3 tools/power_profile.py <capture.log> --calibrate
                                      [--near-cm CM] [--pair-ms MS]

--calibrate answers a different question from the same captures: what the BLE
level actually means in metres on THIS reader in THIS room. Every walk-up already
interleaves `range cm=` (UWB ground truth) with `rssi dbm=`, so it pairs them,
prints the level per distance bin with its spread, and scores each candidate open
threshold on how well it separates near from far. That is what should set
WOZ_RSSI_GATE_OPEN_DBM / CLOSE_DBM, which ship as placeholders. No analyzer needed.

Parses the same "[ALAB] t=<us> ev=..." trace aliro_lab.py reads (firmware built
with CONFIG_WOZ_ALIRO_LAB, `lab on`), now including the RSSI power-gate events
(ev=rssi/gate.hold/gate.open/gate.close, dbm=...), and reports per walk-up:

  held    connect -> gate.open (auth done, UWB deliberately dark)
  g->bolt gate.open -> bolt (the latency the gate actually costs at the door)
  c->bolt connect -> bolt (whole walk-up)
  uwb-on  m4 -> gate.close/session end (the window the DW3000 is powered)
  duty    uwb-on as % of the connected time
  rssi    smoothed level at gate.open (dBm)

--ppk merges a power-analyzer CSV export (PPK2-style: header line, then
"<t_ms>,<current_uA>" rows) and adds mean mA over idle / held / uwb-on spans.
Alignment: the largest positive current step in the capture is assumed to be
the DW3000 waking at m4 (--shift SECONDS overrides with a manual offset from
capture start to the first m4). --tag labels every row (e.g. the approach
speed: slow/normal/fast) so runs concatenate into one study CSV.

Exit status: 0 = parsed at least one walk-up, 2 = usage/input error.

## `modules/woz_port/include/`

### [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md)

*No module docstring. First commit: "modules: promote the platform contract to modules/woz_port".*

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md), [`modules/woz_uwb/src/facade/trace.h`](architecture/modules.woz_uwb.src.facade/trace.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md)

### [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)

@file woz_port.h
Portable platform shim: allocates memory, measures uptime and cycle counts, provides sleep stubs
for host tests, and wraps mutexes (no-op on single-threaded host).

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_cirdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_cirdiag.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/fira/fira_session.c`](architecture/modules.woz_uwb.src.fira/fira_session.c.md)

## `modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/`

### [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md)

@file aliro_uwb_adapter.h — reader-device public interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md)

### [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)

@file aliro_uwb_session.h — per-session public interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

## `modules/woz_aliro/include/`

### [`modules/woz_aliro/include/aliro_ble.h`](architecture/modules.woz_aliro.include/aliro_ble.h.md)

Aliro BLE-UWB reader transport: GATT service definition, advertised feature flags, and transport
callbacks connecting the BLE peripheral role to the Aliro protocol handler in aliro_reader.
Callers configure the transport via aliro_ble_prepare (which builds the READ characteristic
payload without touching NimBLE), then register the GATT service returned by
aliro_ble_service_def with the host's combined service table.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md)

Aliro crypto public API: key derivation, AES-GCM secure channels, and wire message
seal/open framing shared by the reader and device sides of an Aliro session.

**used by** [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md), [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md)

### [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md)

Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
parsed by tools/aliro_lab.py into a scored walk-up report. Ships in every Aliro
build (CONFIG_WOZ_ALIRO_LAB defaults y, like the sibling uwbdiag trace) but is
OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
any firmware profiles on demand with no reflash. Set CONFIG_WOZ_ALIRO_LAB=n to
strip it from a hardened production image.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md)

@file aliro_lat.h
Latency tracking for Aliro protocol phases during a walk-up: record BLE_CONNECT as epoch zero,
mark timestamps for each phase, emit a report with elapsed intervals and flight-recorder
diagnostics.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)

**used by** [`modules/woz_aliro/include/aliro_assert_ec.h`](architecture/modules.woz_aliro.include/aliro_assert_ec.h.md), [`modules/woz_aliro/src/aliro_advtag.c`](architecture/modules.woz_aliro.src/aliro_advtag.c.md), [`modules/woz_aliro/src/aliro_assert_ec.c`](architecture/modules.woz_aliro.src/aliro_assert_ec.c.md), [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_prim_psa.c`](architecture/modules.woz_aliro.src/aliro_prim_psa.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_prov.h`](architecture/modules.woz_aliro.include/aliro_prov.h.md)

Persistent reader provisioning storage: identity and credential trust anchors saved to and
loaded from NVS.
Declares aliro_prov_store for committing an identity/trust pair to NVS, and struct
aliro_trust_store, the set of trusted credential public keys against which a presented
credential is authenticated.

**used by** [`modules/woz_aliro/src/aliro_prov.c`](architecture/modules.woz_aliro.src/aliro_prov.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_reader.h`](architecture/modules.woz_aliro.include/aliro_reader.h.md)

**used by** [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_rssi_gate.h`](architecture/modules.woz_aliro.include/aliro_rssi_gate.h.md)

BLE-RSSI ranging power gate: decides when the phone is close enough that arming
UWB ranging is worth the radio's RX power. Pure sample-in/state-out logic (EWMA
smoothing, open/close hysteresis with a close hold-off, optional rise-rate fast
open for fast approaches) so it host-tests without a radio; the reader feeds it
connection RSSI samples and defers Reader-Status-AP-Completed until it opens.

**used by** [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_rssi_gate.c`](architecture/modules.woz_aliro.src/aliro_rssi_gate.c.md)

### [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md)

Aliro step-up (Access Document) phase: builds the mdoc DeviceRequest, unwraps and decrypts the
SessionData DeviceResponse, decodes the CBOR document per spec 7.2/8.4.2, and runs the six-step
Access Document verification of spec 7.4. Reference-completeness codec + verifier; the verdict is
logged and stored, never gates the unlock (the provisioned trust store remains the sole gate).

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md), [`modules/woz_aliro/src/aliro_stepup_parse.c`](architecture/modules.woz_aliro.src/aliro_stepup_parse.c.md)

### [`modules/woz_aliro/include/aliro_assert_ec.h`](architecture/modules.woz_aliro.include/aliro_assert_ec.h.md)

*No module docstring. First commit: "assert: bind the P-256 seam to aliro_prim".*

**depends on** [`modules/woz_aliro/include/aliro_assert.h`](architecture/modules.woz_aliro.include/aliro_assert.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_assert_ec.c`](architecture/modules.woz_aliro.src/aliro_assert_ec.c.md)

### [`modules/woz_aliro/include/aliro_advtag.h`](architecture/modules.woz_aliro.include/aliro_advtag.h.md)

Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1): the 7-byte
GroupResolvingKey-resolvable tag the phone recomputes to identify a reader of interest.

**used by** [`modules/woz_aliro/src/aliro_advtag.c`](architecture/modules.woz_aliro.src/aliro_advtag.c.md)

### [`modules/woz_aliro/include/aliro_assert.h`](architecture/modules.woz_aliro.include/aliro_assert.h.md)

*No module docstring. First commit: "aliro: presence-assertion protocol (HMAC-signed range statement)".*

**used by** [`modules/woz_aliro/include/aliro_assert_ec.h`](architecture/modules.woz_aliro.include/aliro_assert_ec.h.md), [`modules/woz_aliro/src/aliro_assert.c`](architecture/modules.woz_aliro.src/aliro_assert.c.md)

### [`modules/woz_aliro/include/aliro_approach.h`](architecture/modules.woz_aliro.include/aliro_approach.h.md)

@file aliro_approach.h
Configuration and state for approach detection and predictive unlock: unlock/relock thresholds in
centimeters, sample-count dwell times, motor retraction time, scheduling margin, minimum closing
speed, and a flag to enable or disable predictive ToA unlock.

**used by** [`modules/woz_aliro/src/aliro_approach.c`](architecture/modules.woz_aliro.src/aliro_approach.c.md)

## `modules/woz_uwb/src/aliro/include/cherry/`

### [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md)

@file cherry.h — Cherry core (context + device-capabilities) interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_common.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

### [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md)

@file cherry_ccc.h — CCC/Aliro-session interface (seam the adapter drives).

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_common.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

### [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md)

@file cherry_session.h — generic base-session interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_common.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

### [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_common.h.md)

@file cherry_common.h — diagnostics config struct and report forward decl.

**used by** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](architecture/modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md)

## `ports/esp32/components/piv_ccid/include/`

### [`ports/esp32/components/piv_ccid/include/piv_ccid_usb.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid_usb.h.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/components/piv_ccid/piv_ccid_usb.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid_usb.c.md)

### [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid.h.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](architecture/ports.esp32.components.piv_ccid.include/piv_apdu.h.md)  ·  **used by** [`ports/esp32/components/piv_ccid/piv_ccid.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid.c.md), [`ports/esp32/components/piv_ccid/piv_ccid_usb.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid_usb.c.md)

### [`ports/esp32/components/piv_ccid/include/piv_identity.h`](architecture/ports.esp32.components.piv_ccid.include/piv_identity.h.md)

*No module docstring. First commit: "piv: gate macOS unlock on fresh presence".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](architecture/ports.esp32.components.piv_ccid.include/piv_apdu.h.md)  ·  **used by** [`ports/esp32/components/piv_ccid/piv_ccid_usb.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid_usb.c.md), [`ports/esp32/components/piv_ccid/piv_identity.c`](architecture/ports.esp32.components.piv_ccid/piv_identity.c.md)

### [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](architecture/ports.esp32.components.piv_ccid.include/piv_apdu.h.md)

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**used by** [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](architecture/ports.esp32.components.piv_ccid.include/piv_ccid.h.md), [`ports/esp32/components/piv_ccid/include/piv_identity.h`](architecture/ports.esp32.components.piv_ccid.include/piv_identity.h.md), [`ports/esp32/components/piv_ccid/piv_apdu.c`](architecture/ports.esp32.components.piv_ccid/piv_apdu.c.md), [`ports/esp32/components/piv_ccid/piv_ccid.c`](architecture/ports.esp32.components.piv_ccid/piv_ccid.c.md)

## `modules/woz_nfc/include/woz_nfc/`

### [`modules/woz_nfc/include/woz_nfc/transport.h`](architecture/modules.woz_nfc.include.woz_nfc/transport.h.md)

Woz NFC transport seam.
One reader backend is selected at build time (Kconfig choice WOZ_NFC_TRANSPORT):
the upstream ST25R/RFAL transport, the in-tree PN532 transport, or none. The
add-on application calls these five functions instead of a concrete transport
class; the selected backend supplies the definitions. The semantics mirror the
upstream NfcTransportRfal public API exactly:
- Init():  bring up the bus/PAL. Failure is logged by the caller but not fatal.
- Start(): begin polling for a User Device. May be called again after Stop().
- Stop():  cease polling and switch the RF field off.
- Send():  asynchronous. Queues one APDU for the activated device and returns;
the response is delivered later via AliroStack::HandleSessionData()
from the Aliro workqueue. Returns ALIRO_INVALID_STATE when no
device is activated.
- Terminate(): the stack is done with the session; drop the device and return
to polling. Does not call back into the stack.
The backend owns the session lifecycle in the other direction: on ISO-DEP
activation it calls AliroStack::CreateSession(ConnectionHandle::Nfc()), on
device loss or exchange failure DestroySession(), both from the Aliro
workqueue, matching the upstream RFAL transport's threading.

**used by** [`modules/woz_nfc/src/transport_none.cpp`](architecture/modules.woz_nfc.src/transport_none.cpp.md), [`modules/woz_nfc/src/transport_pn532.cpp`](architecture/modules.woz_nfc.src/transport_pn532.cpp.md), [`modules/woz_nfc/src/transport_rfal.cpp`](architecture/modules.woz_nfc.src/transport_rfal.cpp.md)

## `host/macos-ctk/Extension/`

### [`host/macos-ctk/Extension/PIVCodec.swift`](architecture/host.macos-ctk.Extension/PIVCodec.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

### [`host/macos-ctk/Extension/PIVTransport.swift`](architecture/host.macos-ctk.Extension/PIVTransport.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

### [`host/macos-ctk/Extension/Token.swift`](architecture/host.macos-ctk.Extension/Token.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

### [`host/macos-ctk/Extension/TokenDriver.swift`](architecture/host.macos-ctk.Extension/TokenDriver.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

### [`host/macos-ctk/Extension/TokenSession.swift`](architecture/host.macos-ctk.Extension/TokenSession.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

## `host/macos-ctk/Tests/`

### [`host/macos-ctk/Tests/PIVCodecTests.swift`](architecture/host.macos-ctk.Tests/PIVCodecTests.swift.md)

*No module docstring. First commit: "piv: add macOS CryptoTokenKit UWB-only profile".*

## `integration/homeassistant/scripts/`

### [`integration/homeassistant/scripts/ha-setup.sh`](architecture/integration.homeassistant.scripts/ha-setup.sh.md)

One command from nothing to a working OpenAliro Home Assistant agent.
Generates the broker TLS material, installs it into the Home Assistant
Mosquitto add-on over SSH, writes the agent configuration, and runs doctor.
Every step is idempotent: re-running repairs whatever drifted.
Override any default with an environment variable, for example
HA_SSH=my-hass BROKER_HOST=hass.lan ./ha-setup.sh

## `integration/homeassistant/tools/`

### [`integration/homeassistant/tools/package_component.py`](architecture/integration.homeassistant.tools/package_component.md)

Build a local OpenAliro custom-component archive without publishing it.

## `modules/woz_aliro_ecp/src/`

### [`modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp`](architecture/modules.woz_aliro_ecp.src/nfc_prop_ecp.cpp.md)

NFC Type A proprietary callback implementation for Aliro Express unlock (tap-to-unlock without
Face ID). Emits a CRC_A–checksummed ECP frame carrying the reader identifier.

## `ports/esp32/components/aliro_ble/`

### [`ports/esp32/components/aliro_ble/aliro_ble.c`](architecture/ports.esp32.components.aliro_ble/aliro_ble.c.md)

NimBLE-backed BLE transport for the Aliro reader: GAP advertising, the Aliro GATT service,
and an L2CAP connection-oriented channel (CoC) used to carry Aliro protocol messages.
Supports two bring-up modes: a standalone NimBLE host (aliro_ble_start) and attachment to a
host already owned and synced by another stack such as esp-matter (aliro_ble_prepare +
aliro_ble_start_attached). Tracks CoC channels per connection handle in a fixed-size table
and exposes send/receive plus reader-status notification helpers to the rest of the Aliro
reader.

## `release/esp32-matter-lock/`

### [`release/esp32-matter-lock/flash.sh`](architecture/release.esp32-matter-lock/flash.sh.md)

flash.sh — program the openaliro ESP32-S3 Matter lock (single merged image at
offset 0x0) with esptool. See FLASH.md for wiring and first run.
Usage:  bash flash.sh [PORT]       e.g.  bash flash.sh /dev/ttyACM0

## `release/nrf5340dk/`

### [`release/nrf5340dk/flash.sh`](architecture/release.nrf5340dk/flash.sh.md)

flash.sh — program the openaliro nRF5340 DK firmware (both cores) over the
DK's on-board J-Link, using nrfutil. See FLASH.md for setup and first run.
Usage:  bash flash.sh [JLINK_SERIAL_NUMBER]

## `scripts/`

### [`scripts/bootstrap.sh`](architecture/scripts/bootstrap.sh.md)

bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream.
Fetches everything the build needs from public GitHub into ./workspace
(git-ignored), then applies our integration patches on top. It never reads from
any other local checkout — a clean upstream fetch every time.
Fetches (all public):
- Nordic add-on  ncs-door-lock-and-access-control @ the pin below
- NCS v3.3.0 + Zephyr + every module (via the add-on's own west manifest)
The NCS v3.3.0 toolchain it needs is installed here too, once per machine, so
a clone reaches a build in one command instead of three.
Usage:  scripts/bootstrap.sh                       # workspace in ./workspace
ALIRO_WS=/big/disk/ws scripts/bootstrap.sh # put the multi-GB workspace elsewhere

### [`scripts/build.sh`](architecture/scripts/build.sh.md)

build.sh {build|rebuild|flash|flash-erase|build-flash} — build the Aliro
NFC+UWB image from the self-contained ./workspace. Run scripts/bootstrap.sh first.
Layers our modules + ISC dw3000 onto the fetched add-on via out-of-tree
overlays. Output → ./build (git-ignored).
Incremental by default — a full from-scratch (pristine) build runs only when it
has to: first build, changed build flags (UWB chip / self-test / config), or
when you ask for one. A preflight first checks the workspace is bootstrapped.
scripts/build.sh build                  # incremental where safe (fast)
scripts/build.sh rebuild                # force a clean pristine build
PRISTINE=1 scripts/build.sh build       # same as rebuild
UWB_SELFTEST=1 scripts/build.sh build   # one-shot boot self-test, no iPhone (diagnostic)
PRETTY=1 scripts/build.sh build         # curated/clean console (reversible; default verbose)
ALIRO_SOURCE=0 scripts/build.sh build   # legacy Nordic Aliro binary fallback
UWB_CHIP=dw3720 scripts/build.sh build  # select the plugged-in UWB chip (default: dw3000)

### [`scripts/docs-publish.sh`](architecture/scripts/docs-publish.sh.md)

docs-publish.sh — snapshot the rendered site/ onto the local gh-pages branch.
The site is a build artifact and never lives on main; what gets published is a
snapshot branch that holds site/'s contents at its root. This script only moves
the LOCAL gh-pages ref — pushing it (`git push origin gh-pages`) stays a human
step on purpose. Run it through `make docs-publish`, which rebuilds the site
first so a stale or partial tree can never be snapshotted.
Guards, in order:
- site/index.html and site/.nojekyll must exist (the build completed);
- docs/ must be clean: if the rebuild just changed the committed pages, they
must be committed first, so every snapshot corresponds to a commit;
- an existing gh-pages branch is reused only when it is one of our
snapshots ("docs site …") — a real branch by that name is never eaten;
- the snapshot must actually contain index.html and .nojekyll;
- each snapshot chains to the previous one, so the push fast-forwards.
Nothing here checks out a branch or touches the working tree: the snapshot is
built through a throwaway index, so it is safe to run from any worktree, with
any branch checked out, dirty or not.

### [`scripts/docs.sh`](architecture/scripts/docs.sh.md)

docs.sh — build the documentation site into site/.
Two generators write into the same output directory, in this order:
1. the subsystem tree + guides + search shell   -> site/*.html
2. doxygen (docs/Doxyfile)                      -> site/api/
then a link pass rewrites cross-document links so the published site has no
dead ends, and the freshness gate confirms the committed docs/ tree matches
the source. Run it through `make docs`.
Nothing here needs the NCS toolchain or hardware.

### [`scripts/flash_html.py`](architecture/scripts/flash_html.md)

Render a release FLASH.md into a self-contained FLASH.html.

The markdown file stays the single source of truth; this wraps its rendered
body in an embedded stylesheet (light + dark, no external assets) so the
bundle ships a guide that reads well in a browser. The output is committed
next to its source, so regenerate after editing a FLASH.md:

    pip install markdown==3.8
    python3 scripts/flash_html.py release/*/FLASH.md

Output is deterministic (no timestamps): it only changes when the source does.

### [`scripts/presence_runtime.py`](architecture/scripts/presence_runtime.md)

Build the minimal, deterministic presence runtime transfer archive.

### [`scripts/test-runner.sh`](architecture/scripts/test-runner.sh.md)

Pretty umbrella runner for every host-side suite: one banner, live per-check
rows, a per-suite summary table, and suite timings. The suites themselves are
unchanged — this only orchestrates and renders their existing output:
firmware (C host)      tests/host/run.sh        the KAT suite + the lab python suite
shared core (C host)   ports/esp32/test/run.sh  reader/stepup/crypto/... stages
web twin               scripts/twin-suite.sh    constant-drift gate + WASM selftest
Default: suites run in parallel, output replayed in order when done.
SERIAL=1 streams them live, one at a time. SUITES="firmware shared" scopes.
Exit is nonzero if any suite fails. Colour off when not a TTY or NO_COLOR.

### [`scripts/toolchain.sh`](architecture/scripts/toolchain.sh.md)

toolchain.sh — what the CI gates need, whether this host has it, how to get it.
`make verify` runs eighteen CI gates and skips loudly when a gate's tool is
absent. Skipping loudly is honest, but it leaves the reader to work out what
to install, from where, and at which version. That is this script: one
manifest, two modes.
scripts/toolchain.sh            report every tool, its gate, and its status
scripts/toolchain.sh install    install the missing ones, after confirming
Nothing is installed without being printed first and agreed to. `install`
shows the exact command list and waits for a y; -y answers it in advance for
unattended use.
Versions matter for four of these. clang-format and clang-tidy change their
output between releases, so a host one version off the CI pin fails a gate
that CI passes (or worse, the reverse). Those rows carry the pin CI uses and
say so when the host disagrees.
Out of scope, same boundary as verify.sh: the firmware toolchains. NCS (~6.5
GB, `make bootstrap`) and ESP-IDF are per-target installs with their own
documented procedures — see docs/set-up.md. This covers the host gates only.
Adding a gate to verify.sh without adding its tool here is caught: `check`
reads verify.sh's own gate_need + gate_need_py tables and fails on any name it
cannot explain, and fails again if either table stops parsing. What it does
NOT catch is a row here that no gate needs any more, and none of it runs in
CI — only when someone runs `make tools`.
Env:
ASSUME_YES=1   same as `install -y`
NO_COLOR=1     plain output

### [`scripts/twin-suite.sh`](architecture/scripts/twin-suite.sh.md)

The web-twin suite for the umbrella runner (make check): the constant-drift
gate (always) plus the WASM twin's node self-test against the committed
web-twin/twin.js (when node is present). No rebuild here — regenerating
twin.js needs a pinned emsdk and is CI's byte-diff staleness gate; this only
proves the committed firmware artifact still passes its scenario.

### [`scripts/twin-wasm.sh`](architecture/scripts/twin-wasm.sh.md)

Build the web twin's firmware: modules/woz_uwb + the tests/host shim compiled
to WASM (Emscripten), driven by web-twin/twin_glue.c. Output is a single
self-contained web-twin/twin.js (MODULARIZE + SINGLE_FILE: the .wasm rides
embedded, so the page keeps working from file:// and the site copy stays a
flat file pair). The compile is path-prefix-mapped for reproducibility: the
same emsdk version must produce a byte-identical twin.js on any machine,
which is what lets CI rebuild and diff it as a staleness gate.

### [`scripts/verify.sh`](architecture/scripts/verify.sh.md)

Pre-push sweep: every CI gate that a host can run, in one shot.
The point of this script is that "it passed locally" and "it will pass CI"
mean the same thing. Each row below is one CI *job* (not one workflow —
tooling.yml and workflow-lint.yml each contribute several), running the same
command that job runs. Adding a job to .github/workflows/ without adding it
here re-opens the gap this script exists to close.
Out of scope, deliberately: firmware-builds.yml and release.yml. They need
ESP-IDF and NCS (~6.5 GB of toolchain) and take tens of minutes — not a push
gate. `make build` covers them once the toolchain is bootstrapped.
The gates run in lanes, several at once, because serially they are ~83s of
work on a machine with eight cores. A short serial tripwire goes first, so a
formatting slip still stops the sweep about four seconds in; then the
expensive gates run together and the sweep costs its slowest lane rather than
the sum of all of them. Measured back to back on an idle host: 83s serial,
34s in lanes, and 72s in lanes with cbmc on against 147s serial.
SERIAL=1 puts it back to one gate at a time, for a busy machine or for reading
a confusing failure in order.
One gate does not run by default: cbmc. At 64s it is twice the rest of the
sweep put together, spent on the gate whose input moves least — the wire
parsers it proves have been stable for months, and the fuzz gate exercises the
same code every run. WITH_CBMC=1 turns it on, taking the sweep to ~72s.
It still gets a summary row saying it did not run. cbmc.yml has no path
filter, so the PR runs it whatever happened here; a gate that quietly
disappears from the sweep is the exact failure this script exists to prevent.
A gate whose tool is missing FAILS the sweep. It says so on its row, it is
counted apart from a hand-scoped SKIP=, and the run exits nonzero. Anything
softer is the original bug wearing a warning label: CI runs that gate whatever
this host has installed, so "could not check" has to read as "not verified",
not as "fine". `make tools-install` is the fix; SKIP="<gate>" is the override
for someone who has decided to accept the gap.
Env:
WITH_CBMC=1        also run the cbmc proof (off by default, see above)
SERIAL=1           one gate at a time, fail-fast, instead of lanes
SKIP="cbmc fuzz"   space-separated gate names to leave out of this run
COV_MIN=90         line-coverage floor, matching host-tests.yml
NO_COLOR=1         plain output (colour is the default, pipe or not)
FAIL_TAIL=40       lines of a failing gate's log to show inline

### [`scripts/ws-seed.sh`](architecture/scripts/ws-seed.sh.md)

ws-seed.sh — give this git worktree its own NCS workspace, cheaply.
Frequent branch-bouncing over a single shared workspace is a trap: the tree
holds one patch state at a time (last bootstrap wins), so a build from the
wrong worktree silently compiles another branch's patches. This seeds a
per-worktree workspace at the default path ($TREE/workspace) so build.sh picks
it up with no env var, and each worktree stays self-contained.
Cheap because it uses an APFS copy-on-write clone (cp -c): the clone shares
every block with the primary and costs ~0 extra disk until a patched file
diverges. Cleanup is automatic — the workspace lives inside the worktree, so
deleting the worktree deletes it (see `make ws-clean`).

## `web-twin/`

### [`web-twin/check_constants.py`](architecture/web-twin/check_constants.md)

Verify that the web-twin's hardcoded firmware constants in index.html stay synchronized with their source definitions. Parses the FW table, reads the cited source lines, and reports any mismatches or missing citations.

### [`web-twin/twin_glue.c`](architecture/web-twin/twin_glue.c.md)

@file twin_glue.c — WASM entry points: the twin page's firmware harness.
Compiled (emcc) with the untouched modules/woz_uwb sources plus the same
tests/host shim the host suite links, so the page runs the real responder:
every block is a genuinely CCM*-encrypted Pre-POLL/POLL/Response/Final/
Final_Data exchange decoded by the firmware's own RX state machine, and the
page reads its decisions through the same facade seam the lock uses.
The peer (iPhone) side comes from tests/host/twin_frames.c — shared with
test_twin.c, so the page and the suite drive the responder identically. The
JS above supplies only the world: target distance, noise, spoof timing, and
the pacing of the five per-block legs (PREPOLL/POLL/TXDONE/FINAL/FINAL_DATA)
so a visitor can single-step a live DS-TWR round.
Distance is injected the way physics does it: the initiator-side DS-TWR
intervals ride in the Final_Data as round1 = reply1 + 2*tof and
reply2 = round2 - 2*tof, which makes the firmware's own
(round1*round2 - reply1*reply2)/sum recover exactly tof ticks
(1 tick ~ 15.65 ps, ~4.692 mm — ccc_shim_rx.c final_data_decode).
A Ghost-Peak spoof is a negative-tof block through the same full path.
