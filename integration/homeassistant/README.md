# Home Assistant bridge

`aliro_mqtt_bridge.py` republishes the lock's console log to MQTT as two Home
Assistant Discovery entities:

| Entity | Platform | Source line |
| --- | --- | --- |
| Distance (mm) | `sensor` | `DIST tof=X d=Ymm phone_d=…` (`ccc_shim_rx.c:600`), or the curated `rng  blk=N d=Ymm  tof=X` (`ccc_shim_rx.c:609`) when the build has it |
| Access | `event`, types `granted` / `denied` | `ACCESS GRANTED` / `ACCESS DENIED` (`access_manager`) |

Nothing on the device changes. The bridge only reads the console, so it runs
alongside the existing Apple Home and Wallet setup without taking a Matter
fabric slot.

Guides on the doc site: [setup and troubleshooting][guide] for running it, and
[internals][internals] for the console contract, module map, and test layout.

[guide]: https://openaliro.github.io/openaliro/home-assistant.html
[internals]: https://openaliro.github.io/openaliro/home-assistant-internals.html

## The optional `HA=1` firmware build

Separate from the bridge, `HA=1` opts into a firmware variant that exposes the
same information over Matter instead of over the console: a DoorLock
`LockOperation` event, a UWB-proximity occupancy endpoint, and
`CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE`. The data-model patches live in
`ports/nrf5340dk/patches/`, so this is an nRF5340 DK variant only. It needs both
halves, because the bootstrap step is what applies those patches:

```
make bootstrap HA=1
make nrf-build HA=1
```

**Not hardware-validated.** This has never been run on a board. It changes the
Matter data model of the lock, so an already-commissioned controller may need
the lock re-commissioned to pick the new endpoint up, and
`CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE` is disabled upstream pending
connectedhomeip issue 38222 (a TC-DRLK-2.3 certification failure). Default
builds are unaffected: with `HA` unset, neither the patches nor the overlay are
applied. Treat `HA=1` as untested until someone flashes it.

## Prerequisites

The range line is compiled behind `CONFIG_WOZ_PRETTY_SHELL` and gated at runtime
by `uwb_rxdiag_rng_get()`, so issue `aliro frames on` on the shell first.
Without it the access events still flow but distance stays unpublished.

```
pip install paho-mqtt pyserial   # pyserial only for a real serial port
```

## Usage

```
# live, against a broker (--broker defaults to localhost)
aliro_mqtt_bridge.py --port /dev/tty.usbmodem1234 --broker mqtt.example.com

# parse only: no broker, no board, reads a captured log on stdin
aliro_mqtt_bridge.py --port - --dry-run < captured.log
```

`--node` (default `aliro-lock`) sets the MQTT node id and the Home Assistant
device name, so a second lock just needs a different value.

Topics are `aliro/<node>/distance`, `aliro/<node>/access`, and
`aliro/<node>/status` for availability, which is also the last-will topic.
Discovery configs are published retained under `homeassistant/`.

## Notes

Two distance lines exist. The curated `rng blk=… d=…mm tof=…` one-liner is
built only under `CONFIG_WOZ_PRETTY_SHELL` and stays off until `aliro frames
on`, so most builds never print it. The `DIST tof=… d=…mm phone_d=…mm` line
next to it in `ccc_shim_rx.c` is unconditional and carries the same lock-side
distance. Both are parsed, `rng` first, so a block is never counted twice. The
`phone_d` field is the peer's own estimate, goes negative, and is discarded.

This bridge deliberately carries only what Matter does not expose: the UWB
ranging distance and the Aliro credential verdict. Lock state and lock control
belong to Home Assistant's own [Matter integration][matter], which speaks the
door-lock cluster directly. Publishing a second lock entity over MQTT would
leave two entities competing for the same device.

That split is why a lock or unlock driven from a Matter controller prints
`[ZCL]Received command: UnlockDoor` and no `ACCESS` line: it never went
through Aliro credential verification, so there is no verdict to report. Only
`ACCESS GRANTED` / `ACCESS DENIED` from `access_manager` become access events.

For the lock entity itself, commission the board to the Home Assistant fabric.
Matter supports several fabrics at once, so a board already paired with
another ecosystem should be shared from that ecosystem rather than reset and
re-commissioned.

[matter]: https://www.home-assistant.io/integrations/matter/

`--dry-run` prints each topic and payload instead of connecting, which is the
quickest way to check a parser change against a captured log.

## Staged standalone agent

The productized agent is under active development and is kept out of normal
repository targets: use `make ha-test HA=1` for its host test suite. Its package
commands deliberately do **not** require an `HA=1` environment variable:

```bash
openaliro-ha configure
openaliro-ha doctor
openaliro-ha run
openaliro-ha replay <sanitized-capture> --json
openaliro-ha version
```

### One-step setup

`make ha-setup HA=1` does the whole first-run sequence: it generates the broker
TLS certificate, installs it into the Home Assistant Mosquitto add-on over SSH,
sets the add-on's certfile, keyfile, and login, restarts it, writes the agent
configuration, and finishes with `doctor`. Every step is idempotent, so
re-running it repairs a broken state rather than duplicating one; the
certificate is reused while it still has a month of life and still matches the
broker name.

It defaults to the SSH alias `homeassistant`, the broker name
`homeassistant.local`, MQTT user `openaliro_agent`, and device `front-door`.
Override any of them:

```bash
make ha-setup HA=1                                  # defaults
HA_SSH=hass BROKER_HOST=hass.lan make ha-setup HA=1  # different host
```

The password is read from a prompt (or `MQTT_PASSWORD`) and written to
`~/.config/openaliro-ha/mqtt-password` with mode 0600, and the config records
only that path. Nothing needs exporting into the shell before `run`, and the
password never reaches a process listing. `configure` accepts the same values
as flags (`--mqtt-host`, `--mqtt-password-file`, and so on) when a script needs
to drive it without prompts.

If the broker stops accepting TLS, check `/ssl` on the Home Assistant box
first: Mosquitto's TLS listener never starts when `certfile` or `keyfile` is
missing, and port 8883 refuses connections while 1883 stays open. Re-running
`make ha-setup HA=1` restores them.

`configure` records a hash of the selected USB interface rather than its raw
USB serial number or current device path. It probes the selected console before
writing a user-only configuration file. `doctor` checks that configuration, the
console protocol, and the configured MQTT/TLS path; it never prints broker
secrets, device paths, raw console lines, or USB serial numbers.

The agent is not a Home Assistant custom component yet. The current supported
runtime path remains MQTT Discovery; direct Home Assistant serial ownership and
the custom integration require their own runtime validation.

## Direct Home Assistant integration beta

`custom_components/openaliro/` now contains the direct-serial beta: a manual
config flow, Distance sensor, and Access event entity. It deliberately does not
create a lock-control entity or expose peer, credential, or raw-console data.
It offers device automation triggers for access granted and access denied.
Its diagnostics contain only the hashed-identity presence, baud rate, current
session state, and approved observation availability; they omit serial paths,
USB serials, and console text.

It is not a distributable custom-component archive yet. A release archive must
either bundle the separately built `openaliro-home-assistant` library or install
that tested library through a supported dependency path. Direct serial ownership
on Home Assistant OS, Container, and Core also remains a release gate.
