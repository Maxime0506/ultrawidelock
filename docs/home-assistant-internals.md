# Home Assistant internals

How the bridge is built, what contract it holds with the firmware console, and
what to check when you change either side. The user-facing guide is
[Home Assistant](home-assistant.md).

## Three processes, two machines

```
nRF5340 DK  --USB serial-->  agent  --MQTT/TLS-->  broker  -->  Home Assistant
                          (your machine)      (Mosquitto add-on)
```

The **agent** (`openaliro-ha`) owns the serial port, parses console lines, and
publishes observations. The **broker** routes messages and understands nothing
about locks. Home Assistant subscribes. Everything is a broker client,
including a debugging subscription to `aliro/#`.

Matter is a fourth path that touches none of the above: Home Assistant talks to
the board directly over Thread. That is why lock control survives a dead agent.

The TLS setup exists only because the agent is remote from the broker. Running
the agent on the Home Assistant host would make it a localhost connection and
retire most of that machinery.

## The console contract

Everything the bridge knows comes from three line shapes. Two of them are
distance, and choosing the wrong one is how the sensor silently stayed empty.

| Line | Source | Emitted |
|---|---|---|
| `rng  blk=%-3u d=%dmm  tof=%d` | `ccc_shim_rx.c` | Only under `CONFIG_WOZ_PRETTY_SHELL`, and only after `aliro frames on` |
| `DIST tof=%d d=%dmm phone_d=%dmm rep1=%u rnd2=%u rnd1=%u rep2=%u` | `ccc_shim_rx.c`, nine lines earlier | Unconditionally, every ranging block |
| `ACCESS GRANTED` / `ACCESS DENIED` | `access_manager`, in the vendor application | Once per completed Aliro transaction |

Both distance lines carry the same lock-side value. The parser prefers `rng`
and falls back to `DIST`, so a block is never counted twice. `phone_d` is the
peer's own estimate, goes negative, and is discarded.

A Matter or app driven unlock logs `[ZCL]Received command: UnlockDoor` and no
`ACCESS` line, because it never went through credential verification. There is
no verdict to report, so no event is raised.

Anything unmatched is dropped. That is the redaction boundary: credential
identifiers, frame dumps, and key material appear on the console and must never
reach an observation.

## Module map

Under `integration/homeassistant/src/openaliro_ha/`:

| Module | Responsibility |
|---|---|
| `parser.py` | Console line to typed observation. The only place patterns live. |
| `models.py` | `DistanceReading`, `AccessEvent`, `CompatibilityRangeReading`. `block` is optional because `DIST` carries none. |
| `serial_transport.py` | Port discovery, stable non-reversible USB identity, exclusive open |
| `serial_session.py` | Lifecycle: open, probe, stream or poll, reconnect with backoff |
| `compatibility.py` | The `aliro range` polling path when streaming is unavailable |
| `config.py` | Versioned TOML, validation, redacted rendering |
| `mqtt.py` | TLS, authentication, Discovery payloads, availability, publishing |
| `agent.py` | Orchestration, `doctor`, and distance throttling |
| `cli.py` | `configure`, `doctor`, `run`, `replay`, `version` |

`integration/homeassistant/aliro_mqtt_bridge.py` is the older single-file
bridge. It parses the same lines and is kept in step by a parity test, so a
pattern change has to land in both.

## Two design decisions worth knowing

**Ports are identified by hash, not path.** `configure` records a digest of the
USB identity rather than the device path or raw serial number, and the config
stores `serial_port = "auto"`. Replugging into a different port keeps working,
and the config carries nothing sensitive.

**Distance is throttled.** Ranging emits a reading every block, roughly every
192 ms, which is far more than Home Assistant needs. `_DistanceThrottle` in
`agent.py` publishes at most once per second, but lets a change of 100 mm or
more through immediately, so an approach or retreat is never delayed behind the
interval. Both constants are module level.

## Topics

```
aliro/<device>/distance   millimetres, not retained
aliro/<device>/access     granted | denied, not retained
aliro/<device>/status     online | offline, retained, also the last will
homeassistant/sensor/<device>/distance/config   retained discovery
homeassistant/event/<device>/access/config      retained discovery
```

Discovery is re-announced on every reconnect, so a broker restart does not
strand the entities.

## The ESP32 firmware speaks the same contract

`ports/esp32/apps/matter-lock/main/ha_mqtt.c`, behind `CONFIG_ENABLE_HA_MQTT`
(default n), publishes those five topics from the board, so the ESP32 needs no
agent. It is a reimplementation of `mqtt.py`, not a shared one, and the two must
be changed together:

| Held in step | Where |
|---|---|
| Topics, payloads, QoS, retain, last will | `mqtt.py:52-87`, `:194`, `:226`, `:232` |
| Discovery re-announced per connection | `_on_connect` / `MQTT_EVENT_CONNECTED` |
| Distance throttle, 1 s or a 100 mm change | `_DistanceThrottle` in `agent.py` |
| Device model, `"<target> Aliro lock"` | `DEFAULT_MODEL` on both Python sides, `HA_MQTT_MODEL` in the firmware |

The model is the one field that deliberately differs, so two boards do not look
like the same product in Home Assistant. `test_ha_mqtt.py` pins the default and
the parity with the legacy bridge, and `test_ha_firmware_contract.py` extracts
the format strings out of `ha_mqtt.c`, compiles them on the host, and diffs the
rendered JSON against the agent's — so a discovery field that drifts on either
side fails `make ha-test HA=1` without a board. Topics, QoS and retain flags are
not covered that way; change those on both sides by hand.

Two differences are not drift. The firmware has no console to parse, so it reads
the approach controller's conditioned estimate directly — the same value the
unlock thresholds act on, in centimetres, published as millimetres — rather than
the raw per-block `DIST` line the parser sees. And it takes the access verdict
from the reader's credential trust gate through `aliro_reader_set_access_listener`,
which is where the vendor `ACCESS GRANTED` / `ACCESS DENIED` lines come from on
the nRF5340.

## Testing

```bash
make ha-test HA=1
```

Ten suites under `tests/host/`. They open no serial port, no broker, and no
network: transports are faked and the console is replayed from fixtures.

| Suite | Covers |
|---|---|
| `test_ha_parser.py` | Line patterns, ANSI stripping, parity with the legacy bridge |
| `test_ha_config.py` | Schema, validation, redaction |
| `test_ha_mqtt.py` | TLS options, discovery payloads, availability |
| `test_ha_cli.py` | Subcommands, the interface picker, flag-driven configure |
| `test_ha_agent.py` | Orchestration, `doctor`, distance throttling |
| `test_ha_serial_session.py` | Lifecycle and reconnection |
| `test_ha_serial_transport.py` | Discovery, identity, exclusive open |
| `test_ha_compatibility.py` | The polling fallback |
| `test_ha_stage0.py` | Fixture manifest and redaction rules |
| `test_ha_setup.py` | Shape of the setup script |
| `test_ha_package.py` | Component archive contents |

`HA=1` keeps all of it out of the default test path, so the productization work
cannot break `make test` for someone working on firmware. The cost is that
nothing runs it implicitly: the CI job is separate, and `make coverage` sets the
variable explicitly when measuring these modules.

### Fixtures and redaction

`integration/homeassistant/stage0/captures/` holds the console evidence, with a
manifest declaring each capture's source, ANSI state, and expected
observations. `hardware_uwb_access.log` is verbatim board output with protocol
frame dumps, key material, session identifiers, and Matter transport lines
removed. A test enforces the forbidden patterns, so a capture carrying
credentials or raw frames fails the build.

### Changing a console pattern

1. Add the line to a capture, or add a capture, and update its expected JSON.
2. Update `parser.py` and `aliro_mqtt_bridge.py` together; the parity test
   fails otherwise.
3. Add a drift test that renders the line from the firmware's own format
   string, as `test_dist_diagnostic_format_drift` does. A pattern pinned to a
   literal rots the moment the format string moves.

## Packaging

`make ha-package HA=1` builds the custom-component archive, vendoring the
shared library so it installs as one directory. The component is a beta and is
not required for the MQTT path, which works through Discovery alone.
