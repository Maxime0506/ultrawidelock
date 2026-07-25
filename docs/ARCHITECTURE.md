<!-- generated documentation — edit the source, not this file -->
# openaliro — architecture

Every subsystem on one page, in reading order: entry points (nothing imports them) first, then the machinery they drive. Each section is the subsystem's own prose, what it exposes, and how the pieces depend on each other; headings link to the full per-module reference under [`architecture/`](architecture/).

```mermaid
flowchart LR
  modules.woz_aliro.include --> modules.woz_aliro.src
  modules.woz_aliro.src --> modules.woz_aliro.include
  modules.woz_aliro.src --> modules.woz_port.include
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_aliro.src --> modules.woz_uwb.src.facade
  modules.woz_aliro_stack.src --> modules.woz_aliro_stack.src.protocol
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
```

## `modules/woz_uwb/src/aliro/`

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md)

@file aliro_uwb_msg.c — setup/notification message codec.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/ccc/aliro_round_config.h`](architecture/modules.woz_uwb.src.ccc/aliro_round_config.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_util.h`](architecture/modules.woz_uwb.src.facade/woz_util.h.md)

### [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.c.md)

Device-side UWB ranging-service setup codec: parses the reader's M1 and M3
setup messages, picks the device's answer to M1 (select_m2), and builds the M2
and M4 replies. The inverse of the reader path in aliro_uwb_msg.c, written over
the same TLV parser and builder helpers. No crypto and no session state, so a
host loopback can drive the real reader codec end to end.

**depends on** [`modules/woz_uwb/src/aliro/aliro_device_uwb.h`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md)

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

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md)

@file aliro_uwb_msg_builder.h — big-endian TLV message builder.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md)

@file aliro_uwb_msg_parser.h — TLV attribute iteration and big-endian reads.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.c.md)

### [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md)

@file aliro_uwb_msg_spec.h — UWB ranging-service framing constants.

**used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.h`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md)

### [`modules/woz_uwb/src/aliro/aliro_device_uwb.h`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.h.md)

Device/initiator side of the UWB ranging-service setup codec: the interface for
parsing the reader's M1 and M3 and building the device's M2 and M4. Declares the
decoded views of M1 and M3, the parameter structs the two builders take, and
select_m2, which chooses a config and slot layout from what M1 offered. Pure
TLV, no crypto and no session state, so it is host-testable against the reader's
own codec by loopback.

**depends on** [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_spec.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_device_uwb.c`](architecture/modules.woz_uwb.src.aliro/aliro_device_uwb.c.md)

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

### [`modules/woz_aliro/src/aliro_device.c`](architecture/modules.woz_aliro.src/aliro_device.c.md)

Aliro initiator (User-Device) session machine: the implementation behind
aliro_device.h. Feeds one reader command at a time through
aliro_device_on_command, which parses AUTH0/AUTH1/EXCHANGE with the inverse
codec, runs the mirror of the reader's key schedule (ephemeral ECDH, the two
ECDSA transcripts, the session salt) and returns the sealed response. Owns the
two AES-256-GCM channels the device holds, the Access-Protocol channel and the
BleSK ranging channel, both split out of the same 160-byte key block, plus the
standard-path derivation factored EC-free so host tests can drive it with a
supplied shared secret.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_device.h`](architecture/modules.woz_aliro.include/aliro_device.h.md), [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/src/aliro_apdu.h`](architecture/modules.woz_aliro.src/aliro_apdu.h.md)

### [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md)

Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.

**depends on** [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md), [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md), [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)

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

### [`modules/woz_aliro/src/aliro_ble_central.c`](architecture/modules.woz_aliro.src/aliro_ble_central.c.md)

Platform-free half of the device-side BLE transport declared in
aliro_ble_central.h: decodes the reader's 0xFFF2 service-data advert, decodes
the reader-SPSM GATT READ payload (SPSM, supported protocol versions, feature
mask), and assembles the BleSK salt from the version list the reader actually
published rather than from a compiled-in constant. No BLE stack calls and no
allocation, so it builds on the host and is checked byte for byte against the
reader's own emitters.

**depends on** [`modules/woz_aliro/include/aliro_ble_central.h`](architecture/modules.woz_aliro.include/aliro_ble_central.h.md)

### [`modules/woz_aliro/src/aliro_device_apdu.c`](architecture/modules.woz_aliro.src/aliro_device_apdu.c.md)

Implementation of the device-side Access-Protocol wire codec declared in
aliro_device_apdu.h: ISO7816 case-4 unwrapping, status-word appending, parsers
for the reader's AUTH0, AUTH1 and EXCHANGE command TLVs, and builders for the
three device responses. Every function is bounds-checked byte manipulation over
caller-owned buffers with no allocation, so it round-trips against the reader's
own builders and parsers in aliro_apdu.c under the host tests.

**depends on** [`modules/woz_aliro/include/aliro_device_apdu.h`](architecture/modules.woz_aliro.include/aliro_device_apdu.h.md)

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

*No module docstring. First commit: "approach: predict time-of-arrival, open the bolt at arrival".*

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

**used by** [`modules/woz_aliro/include/aliro_device_apdu.h`](architecture/modules.woz_aliro.include/aliro_device_apdu.h.md), [`modules/woz_aliro/src/aliro_apdu.c`](architecture/modules.woz_aliro.src/aliro_apdu.c.md), [`modules/woz_aliro/src/aliro_device.c`](architecture/modules.woz_aliro.src/aliro_device.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/src/aliro_hash.h`](architecture/modules.woz_aliro.src/aliro_hash.h.md)

Streaming SHA-256 (FIPS 180-4) implementation used by the Aliro crypto layer.
Declares struct aliro_sha256, the incremental hash context used across init/update/finish
calls.

**used by** [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_hash.c`](architecture/modules.woz_aliro.src/aliro_hash.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md)

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

## `modules/woz_uwb/src/driver/`

### [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md)

@file uwb_rxdiag.c — Diagnostic RX/TX event tallies + ranging heartbeat.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

### [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md)

@file uwb_isr.c — DW3000 interrupt-callback registration (implementation).

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/driver/uwb_isr.h`](architecture/modules.woz_uwb.src.driver/uwb_isr.h.md), [`modules/woz_uwb/src/facade/trace.h`](architecture/modules.woz_uwb.src.facade/trace.h.md)

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

## `modules/woz_aliro_stack/src/`

### [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md), [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md)

### [`modules/woz_aliro_stack/src/advertising_core.c`](architecture/modules.woz_aliro_stack.src/advertising_core.c.md)

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

### [`modules/woz_aliro_stack/src/aliro_stack.cpp`](architecture/modules.woz_aliro_stack.src/aliro_stack.cpp.md)

Clean-room implementation of the Nordic Aliro public API used by this app.
Protocol constants and wire formats come from Aliro Specification 1.0.
No implementation detail from the proprietary archive is used here.

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

### [`modules/woz_aliro_stack/src/advertising_core.h`](architecture/modules.woz_aliro_stack.src/advertising_core.h.md)

Clean-room Aliro BLE advertising primitives.
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

*No module docstring. First commit: "flight-recorder: record/replay real UWB walk-ups".*

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

*No module docstring. First commit: "flight-recorder: record/replay real UWB walk-ups".*

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](architecture/modules.woz_uwb.src.facade/woz_uwb_facade.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

### [`modules/woz_uwb/src/facade/woz_bytes.h`](architecture/modules.woz_uwb.src.facade/woz_bytes.h.md)

*No module docstring. First commit: "port: replace the Zephyr compat shims with a neutral woz_port.h contract".*

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/ccc/ccc_sts.c`](architecture/modules.woz_uwb.src.ccc/ccc_sts.c.md)

### [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md)

@file woz_diag.h — DIAGK(): gate for verbose UWB bring-up diagnostics.

**depends on** [`modules/woz_port/include/woz_log.h`](architecture/modules.woz_port.include/woz_log.h.md)  ·  **used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.c.md)

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

## `modules/woz_uwb/src/shell/`

### [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md)

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](architecture/modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](architecture/modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](architecture/modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](architecture/modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](architecture/modules.woz_uwb.src.fira/fira_session.h.md)

## `ports/esp32/apps/matter-lock/main/`

### [`ports/esp32/apps/matter-lock/main/app_main.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_main.cpp.md)

Matter application main: door lock endpoint setup, Matter lifecycle event handling, and (when
CONFIG_ENABLE_ALIRO_BLE_UWB is set) startup/coexistence wiring for the Aliro BLE+UWB reader
alongside the Matter BLE commissioning transport.
Owns the Aliro reader background task (started once on commissioning-complete or at boot if
already commissioned) and the Matter attribute/identify/device-event callbacks required by
esp-matter's node/cluster framework.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](architecture/ports.esp32.apps.matter-lock.main/app_priv.h.md), [`ports/esp32/apps/matter-lock/main/app_shell.h`](architecture/ports.esp32.apps.matter-lock.main/app_shell.h.md), [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](architecture/ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.h.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)

### [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_driver.cpp.md)

Board driver glue for the ESP32 Matter port: button input, WS2812 lock-status LED, and the
Matter attribute-update hook wired into the app's driver layer.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](architecture/ports.esp32.apps.matter-lock.main/app_priv.h.md), [`ports/esp32/apps/matter-lock/main/lock_led.h`](architecture/ports.esp32.apps.matter-lock.main/lock_led.h.md)

### [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md)

ESP32-IDF console shell for the Aliro Matter door lock app: registers status, range, aliro, lock/unlock, codes, factoryreset, and clear commands and runs the REPL.

**depends on** [`ports/esp32/apps/matter-lock/main/app_shell.h`](architecture/ports.esp32.apps.matter-lock.main/app_shell.h.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](architecture/ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)

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

## `modules/woz_aliro_stack/src/protocol/`

### [`modules/woz_aliro_stack/src/protocol/ble_message.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/ble_timeout.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](architecture/modules.woz_aliro_stack.src.protocol/ble_timeout.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_auth.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_select.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md), [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/access_document.c`](architecture/modules.woz_aliro_stack.src.protocol/access_document.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md)

### [`modules/woz_aliro_stack/src/protocol/tlv.c`](architecture/modules.woz_aliro_stack.src.protocol/tlv.c.md)

**depends on** [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

### [`modules/woz_aliro_stack/src/protocol/access_document.h`](architecture/modules.woz_aliro_stack.src.protocol/access_document.h.md)

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

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/nfc_step_up.h`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.h.md)

Aliro 1.0 / ISO 18013-5 NFC step-up message and APDU codecs.

**used by** [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md), [`modules/woz_aliro_stack/src/session.cpp`](architecture/modules.woz_aliro_stack.src/session.cpp.md)

### [`modules/woz_aliro_stack/src/protocol/tlv.h`](architecture/modules.woz_aliro_stack.src.protocol/tlv.h.md)

Minimal strict BER/DER-TLV reader for Aliro APDU payloads.

**used by** [`modules/woz_aliro_stack/src/protocol/ble_message.c`](architecture/modules.woz_aliro_stack.src.protocol/ble_message.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_auth.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_auth.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_select.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_select.c.md), [`modules/woz_aliro_stack/src/protocol/nfc_step_up.c`](architecture/modules.woz_aliro_stack.src.protocol/nfc_step_up.c.md), [`modules/woz_aliro_stack/src/protocol/tlv.c`](architecture/modules.woz_aliro_stack.src.protocol/tlv.c.md)

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

## `ports/esp32/apps/reader/main/`

### [`ports/esp32/apps/reader/main/app_shell.c`](architecture/ports.esp32.apps.reader.main/app_shell.c.md)

ESP32-IDF console shell for the standalone Aliro UWB responder bench app: registers status, range, aliro-start/stop, provisioning, trust, and clear commands and runs the linenoise-based REPL.

**depends on** [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md)

### [`ports/esp32/apps/reader/main/main.c`](architecture/ports.esp32.apps.reader.main/main.c.md)

Woz UWB ranging engine on ESP32-S3 (ESP-IDF) — minimal bring-up app.
Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
polls for a range. With no iPhone/initiator present this proves the SPI +
DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
The demo responder lifecycle + interactive console live in app_shell.c.

**depends on** [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md)

### [`ports/esp32/apps/reader/main/app_shell.h`](architecture/ports.esp32.apps.reader.main/app_shell.h.md)

**used by** [`ports/esp32/apps/reader/main/app_shell.c`](architecture/ports.esp32.apps.reader.main/app_shell.c.md), [`ports/esp32/apps/reader/main/main.c`](architecture/ports.esp32.apps.reader.main/main.c.md)

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

E1 probe of the passive carry verification experiment (see
internal/passive-verify-scoping.md): for every walk-up transaction in the
given "[ALAB]" captures, detrend the per-block trusted-range series, FFT the
residual, and report the carry-motion features (cadence, stride regularity,
approach speed, deceleration, closest approach, residual RMS) plus a
per-window carried/stationary verdict. With two or more labels (one per
carrier, e.g. alice=alice.log bob=bob.log) it also runs leave-one-out
nearest-centroid classification to measure whether the features separate the
carriers — the pre-registered Tier-2 GO bar is >= 80%.

The block duration (and the phone's implied RAN multiplier) is derived from
the range timestamps themselves, so no extra firmware logging is needed.
Exit status: 0 = report produced, 2 = usage/input error.

**depends on** [`tools/aliro_lab.py`](architecture/tools/aliro_lab.md)

### [`tools/aliro_lab.py`](architecture/tools/aliro_lab.md)

Aliro Lab: score a captured reader serial log.

Usage: python3 tools/aliro_lab.py <capture.log> [report.html]

Parses the structured "[ALAB] t=<us> ev=..." trace lines the firmware emits
when CONFIG_WOZ_ALIRO_LAB is enabled (see modules/woz_aliro/src/aliro_lab.h),
groups them into walk-up transactions, and reports phase timings, the flow
taken (fast vs standard), and pass/warn/fail invariant checks — to the
terminal and as a self-contained HTML report (default: <capture.log>.html).

Every check encodes an invariant of this repo's reader implementation (see
internal notes in the check text), nothing else. Exit status: 0 = no failing
check, 1 = at least one FAIL, 2 = usage/input error.

**used by** [`tools/aliro_gait.py`](architecture/tools/aliro_gait.md)

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

Usage:
  flight_recorder.py <capture.log | trace.frc> [corpus_dir]

With a `.log` input the reconstructed trace is written next to it as `.frc`.
With a corpus_dir the frames are written there as `frame_NNNN.bin`. Stdlib only;
the binary format mirrors flight_recorder.h byte for byte.

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

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg_parser.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](architecture/modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](architecture/modules.woz_uwb.src.facade/flight_recorder.c.md), [`modules/woz_uwb/src/facade/trace.h`](architecture/modules.woz_uwb.src.facade/trace.h.md), [`modules/woz_uwb/src/facade/woz_diag.h`](architecture/modules.woz_uwb.src.facade/woz_diag.h.md)

### [`modules/woz_port/include/woz_port.h`](architecture/modules.woz_port.include/woz_port.h.md)

*No module docstring. First commit: "modules: promote the platform contract to modules/woz_port".*

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](architecture/modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](architecture/modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](architecture/modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/fira/fira_session.c`](architecture/modules.woz_uwb.src.fira/fira_session.c.md)

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

**used by** [`modules/woz_aliro/include/aliro_device.h`](architecture/modules.woz_aliro.include/aliro_device.h.md), [`modules/woz_aliro/include/aliro_stepup.h`](architecture/modules.woz_aliro.include/aliro_stepup.h.md), [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_device.c`](architecture/modules.woz_aliro.src/aliro_device.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](architecture/modules.woz_aliro.src/aliro_stepup.c.md)

### [`modules/woz_aliro/include/aliro_lab.h`](architecture/modules.woz_aliro.include/aliro_lab.h.md)

Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
parsed by tools/aliro_lab.py into a scored walk-up report. Ships in every Aliro
build (CONFIG_WOZ_ALIRO_LAB defaults y, like the sibling uwbdiag trace) but is
OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
any firmware profiles on demand with no reflash. Set CONFIG_WOZ_ALIRO_LAB=n to
strip it from a hardened production image.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_lat.h`](architecture/modules.woz_aliro.include/aliro_lat.h.md)

*No module docstring. First commit: "Cut ESP32 walk-up unlock latency: instrument, unblock, and precompute".*

**used by** [`modules/woz_aliro/src/aliro_lat.c`](architecture/modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

### [`modules/woz_aliro/include/aliro_prim.h`](architecture/modules.woz_aliro.include/aliro_prim.h.md)

**used by** [`modules/woz_aliro/src/aliro_advtag.c`](architecture/modules.woz_aliro.src/aliro_advtag.c.md), [`modules/woz_aliro/src/aliro_crypto.c`](architecture/modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_device.c`](architecture/modules.woz_aliro.src/aliro_device.c.md), [`modules/woz_aliro/src/aliro_prim_psa.c`](architecture/modules.woz_aliro.src/aliro_prim_psa.c.md), [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md)

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

### [`modules/woz_aliro/include/aliro_device.h`](architecture/modules.woz_aliro.include/aliro_device.h.md)

Aliro initiator (User-Device) session layer: the device-side counterpart of
aliro_reader.c. Drives the credential-auth handshake from the phone/fob role —
parses the reader's AUTH0/AUTH1/EXCHANGE commands, runs the mirror-image key
schedule (ECDH, the two ECDSA transcripts, the §8.3.1.13 salt), and produces
the sealed responses. The result is the same 32-byte URSK the reader derives.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](architecture/modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_device_apdu.h`](architecture/modules.woz_aliro.include/aliro_device_apdu.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_device.c`](architecture/modules.woz_aliro.src/aliro_device.c.md)

### [`modules/woz_aliro/include/aliro_advtag.h`](architecture/modules.woz_aliro.include/aliro_advtag.h.md)

Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1): the 7-byte
GroupResolvingKey-resolvable tag the phone recomputes to identify a reader of interest.

**used by** [`modules/woz_aliro/include/aliro_ble_central.h`](architecture/modules.woz_aliro.include/aliro_ble_central.h.md), [`modules/woz_aliro/src/aliro_advtag.c`](architecture/modules.woz_aliro.src/aliro_advtag.c.md)

### [`modules/woz_aliro/include/aliro_ble_central.h`](architecture/modules.woz_aliro.include/aliro_ble_central.h.md)

Device-side (User-Device) BLE transport interface: the central/client mirror of
aliro_ble.h. Where the reader advertises 0xFFF2, serves the GATT characteristics
and runs an L2CAP CoC server, the initiator scans, connects, reads the reader's
SPSM/versions, writes its selected version and opens a CoC client to that SPSM.
The platform-free half (advert + READ-payload decoding, BleSK salt assembly)
lives in aliro_ble_central.c and is host-testable; the NimBLE backend for the
transport calls sits in ports/esp32, so a Zephyr bt_gap_*/bt_l2cap_* backend
can be written behind this same header.

**depends on** [`modules/woz_aliro/include/aliro_advtag.h`](architecture/modules.woz_aliro.include/aliro_advtag.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ble_central.c`](architecture/modules.woz_aliro.src/aliro_ble_central.c.md)

### [`modules/woz_aliro/include/aliro_device_apdu.h`](architecture/modules.woz_aliro.include/aliro_device_apdu.h.md)

Device (User-Device) side of the Aliro Access-Protocol wire codec: the inverse
of aliro_apdu.c. Where aliro_apdu builds reader commands and parses device
responses, this parses the reader's AUTH0/AUTH1/EXCHANGE commands and builds
the device's AUTH0/AUTH1/EXCHANGE responses. Pure byte manipulation, no crypto
and no platform dependency, so it is host-KAT verifiable against the reader's
own builders/parsers (round-trip) and the recovered layouts.

**depends on** [`modules/woz_aliro/src/aliro_apdu.h`](architecture/modules.woz_aliro.src/aliro_apdu.h.md)  ·  **used by** [`modules/woz_aliro/include/aliro_device.h`](architecture/modules.woz_aliro.include/aliro_device.h.md), [`modules/woz_aliro/src/aliro_device_apdu.c`](architecture/modules.woz_aliro.src/aliro_device_apdu.c.md)

### [`modules/woz_aliro/include/aliro_approach.h`](architecture/modules.woz_aliro.include/aliro_approach.h.md)

*No module docstring. First commit: "approach: predict time-of-arrival, open the bolt at arrival".*

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

## `modules/woz_aliro_ecp/src/`

### [`modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp`](architecture/modules.woz_aliro_ecp.src/nfc_prop_ecp.cpp.md)

NFC Type A proprietary callback implementation for Aliro Express unlock (tap-to-unlock without
Face ID). Emits a CRC_A–checksummed ECP frame carrying the reader identifier.

## `ports/esp32/apps/initiator/main/`

### [`ports/esp32/apps/initiator/main/main.c`](architecture/ports.esp32.apps.initiator.main/main.c.md)

ESP32-S3 application entry for the Aliro initiator, the User-Device role that
stands in for an iPhone on the bench. Stage 1a wires the BLE transport only: it
starts the NimBLE central, which scans for the reader's 0xFFF2 advert, connects,
reads the reader's SPSM, supported versions and features, writes the version it
selects, and opens the L2CAP channel. It then reports what it learned, including
the BleSK salt those versions imply, and dumps whatever the reader sends. It
stops before AUTH0, because running the transaction needs an Access Credential
the reader trusts and both ends must be provisioned out of band first.

## `ports/esp32/components/aliro_ble/`

### [`ports/esp32/components/aliro_ble/aliro_ble.c`](architecture/ports.esp32.components.aliro_ble/aliro_ble.c.md)

NimBLE-backed BLE transport for the Aliro reader: GAP advertising, the Aliro GATT service,
and an L2CAP connection-oriented channel (CoC) used to carry Aliro protocol messages.
Supports two bring-up modes: a standalone NimBLE host (aliro_ble_start) and attachment to a
host already owned and synced by another stack such as esp-matter (aliro_ble_prepare +
aliro_ble_start_attached). Tracks CoC channels per connection handle in a fixed-size table
and exposes send/receive plus reader-status notification helpers to the rest of the Aliro
reader.

## `ports/esp32/components/aliro_ble_central/`

### [`ports/esp32/components/aliro_ble_central/aliro_ble_central_nimble.c`](architecture/ports.esp32.components.aliro_ble_central/aliro_ble_central_nimble.c.md)

NimBLE central/client backend for the Aliro initiator: the mirror of
components/aliro_ble/aliro_ble.c. That file advertises 0xFFF2, serves the
characteristics and runs a CoC server; this one scans for 0xFFF2, connects,
discovers, reads the reader's SPSM/versions, writes the selected version and
opens a CoC client to that SPSM.

## `ports/esp32/components/aliro_reader/`

### [`ports/esp32/components/aliro_reader/aliro_prov_nvs.c`](architecture/ports.esp32.components.aliro_reader/aliro_prov_nvs.c.md)

NVS-backed persistence for Aliro reader provisioning: loads and stores the serialized reader
identity and trust store built by aliro_prov.c.
Lazily initializes NVS on first use; safe to call alongside aliro_ble's own nvs_flash_init.

### [`ports/esp32/components/aliro_reader/aliro_stepup_worker.c`](architecture/ports.esp32.components.aliro_reader/aliro_stepup_worker.c.md)

*No module docstring. First commit: "esp32: add the Aliro step-up (Access Document) phase".*

## `ports/nrf5340dk/on_target_ec/src/`

### [`ports/nrf5340dk/on_target_ec/src/main.c`](architecture/ports.nrf5340dk.on_target_ec.src/main.c.md)

nRF5340DK on-target self-test for the Aliro device (initiator) EC path: a
minimal Zephyr application that brings up the real PSA backend (nrf_security on
CryptoCell), runs the same credential-auth crypto suite the host tests run, and
prints PASS or FAIL to the DK console. It exists because the host suite proves
the maths against a software curve only; this proves the same vectors on the
silicon that will ship, and it caught a PSA import failure that no host run
could see. Crypto only: no BLE, no UWB, no iPhone.

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
Prereq (once per machine): nRF Connect SDK v3.3.0 toolchain
nrfutil sdk-manager toolchain install --ncs-version v3.3.0
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
ALIRO_SOURCE=1 scripts/build.sh build   # clean-room source stack (discovery slice)
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

*No module docstring. First commit: "web: add the walk-up digital twin as an interactive page".*

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
