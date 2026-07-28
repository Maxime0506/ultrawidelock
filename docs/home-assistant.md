# Home Assistant

Bring the lock into Home Assistant: UWB distance and Aliro access events over
MQTT, lock control over Matter. Beta, and gated behind `HA=1` everywhere.

## What you get

Two independent paths, and you want both.

| Path | Gives you | Depends on |
|---|---|---|
| MQTT bridge (this repo) | Distance sensor in mm, `granted` / `denied` access events, device triggers | An agent process holding the board's serial console |
| [Matter integration][matter] (built into Home Assistant) | Lock and unlock, lock state, auto-relock, wrong-code limit | The board commissioned to the Home Assistant fabric |

On ESP32 the first row has a second implementation: the firmware can publish those
same topics itself over TLS, with no agent process at all. It is off by default and
described in
[`ports/esp32/apps/matter-lock/README.md`](../ports/esp32/apps/matter-lock/README.md).
The agent stays the supported path for the nRF5340, and the rest of this page is
about the agent. Run one publisher per node, never both.

They share nothing at runtime. Lock control keeps working when the agent is
stopped, and distance keeps flowing if Matter is not set up. The split is
deliberate: publishing a second lock entity over MQTT would leave two entities
competing for one device, so the bridge carries only what Matter does not
expose.

[matter]: https://www.home-assistant.io/integrations/matter/

## Before you start

* A board running this firmware. The bridge reads the `aliro` console, which is
  built whenever `CONFIG_SHELL` is on, so a standard `make build` is enough.
  `HA=1` is a separate thing: it layers Matter credential attribution for
  multi-admin setups and changes nothing the bridge reads.
* Home Assistant with the Mosquitto broker add-on installed.
* Passwordless SSH to the Home Assistant host, as a `Host` entry in
  `~/.ssh/config`. The setup script uses it to install certificates and read
  add-on options.
* The agent installed on the machine the board is plugged into:
  `pipx install ./integration/homeassistant`.

## Set up

```bash
make ha-setup HA=1
```

Six steps, all idempotent, ending in `doctor`:

1. check prerequisites,
2. generate the broker TLS certificate, reused while it still has a month of
   life and still matches the broker name,
3. install the certificate and key into `/ssl` on the Home Assistant host,
4. point the Mosquitto add-on at both, set the login, restart it, and wait for
   TLS to answer,
5. probe the console, pick the interface that responds, and write
   `~/.config/openaliro-ha/agent.toml`,
6. run `doctor`.

Re-running repairs a broken state rather than duplicating one, so it is also
the first thing to try when something stops working. The MQTT block is replaced
and any device other than this one is carried over, so a second board added
separately survives. A configuration too damaged to parse is moved to
`agent.toml.unreadable` and named, never discarded quietly.

Defaults suit a stock Home Assistant OS install. Override any of them:

```bash
HA_SSH=hass BROKER_HOST=hass.lan MQTT_USER=agent make ha-setup HA=1
```

| Variable | Default | Meaning |
|---|---|---|
| `HA_SSH` | `homeassistant` | SSH host entry for the Home Assistant machine |
| `BROKER_HOST` | `homeassistant.local` | Broker name, and the certificate's subject alternative name |
| `BROKER_PORT` | `8883` | Broker TLS port |
| `MQTT_USER` | `openaliro_agent` | Broker login the agent uses |
| `DEVICE_ID` | `front-door` | Node name in topics and entity names |
| `CONFIG_DIR` | `~/.config/openaliro-ha` | Config, certificate, and password location |
| `CERT_DAYS` | `825` | Certificate lifetime |
| `MQTT_PASSWORD` | prompted | Set it to run unattended |

The password is written to `$CONFIG_DIR/mqtt-password` with mode 0600, and the
config records only that path. Nothing needs exporting into your shell, and the
password never appears in a process listing.

## Run

```bash
openaliro-ha --config ~/.config/openaliro-ha/agent.toml run
```

The agent holds the serial port exclusively for as long as it runs, so close
any terminal monitor first. It publishes to `aliro/<device>/distance`,
`aliro/<device>/access`, and a retained `aliro/<device>/status`, and announces
both entities through MQTT Discovery. Home Assistant builds the device with no
custom component involved.

Stopping the agent sets `status` to `offline` through the broker's last will,
and Home Assistant marks those two entities unavailable. That is correct
behaviour, not a stuck state.

There is no service wrapper yet, so the agent runs only while you run it.

## Lock control over Matter

The bridge does not provide a lock entity. Commission the board to the Home
Assistant fabric instead. A board already paired with another ecosystem should
be shared from that ecosystem rather than reset, since Matter supports several
fabrics at once, and a reset would drop the Aliro credentials with it.

Development boards present the Matter test attestation, which Home Assistant
rejects by default. Enable **Test Net DCL** in the Matter Server add-on
configuration before pairing. Attestation is checked only while commissioning,
so turning the option off afterwards does not disturb a device already paired.

## When it does not work

Run `openaliro-ha --config ~/.config/openaliro-ha/agent.toml doctor` first: it
checks the config, the console, the broker, and TLS, and names which one failed.

**The broker refuses port 8883 while 1883 still answers.** Mosquitto's TLS
listener never starts when `certfile` or `keyfile` is missing, and it fails
quietly. Look in `/ssl` on the Home Assistant host, then re-run
`make ha-setup HA=1` to restore both files.

**Certificate verification fails.** The name you connect to has to appear in
the certificate's subject alternative name. Set `BROKER_HOST` to exactly the
name in `agent.toml` and re-run setup.

**`configure` finds two identical interfaces.** A single J-Link exposes two CDC
interfaces with the same product string, USB identifiers, and location, and on
macOS neither carries an interface name. The setup probes both and picks the
one that answers; which one that is varies, so do not assume the first. When
neither answers, the reported reason distinguishes a busy port from a board
that is not talking.

**The board says nothing at all.** Confirm it is running this firmware. A board
flashed with something else opens fine and returns no bytes, which looks
identical to a cable problem. Comparing the vector table against the local
build settles it: `nrfjprog --memrd 0x0 --n 16` against the first record of
`build/merged.hex`.

**Distance stays unknown.** Readings only exist during an active UWB ranging
session. An NFC tap alone runs no ranging, so nothing is published. Tap with
UWB enabled on the phone.

**Entities are unavailable.** Check the agent is still running. The retained
`status` topic reports what the broker last saw.
