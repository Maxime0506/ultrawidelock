<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/aliro_mqtt_bridge.py`

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

**depends on** [`tools/tui/src/serial.ts`](../tools.tui.src/serial.ts.md)  ·  **discussed in** [`integration/homeassistant/README.md`](../../../integration/homeassistant/README.md)

## API

### `parse_line(line: str) -> Optional[dict]`
`integration/homeassistant/aliro_mqtt_bridge.py:38`

Return a reading dict for a range or access line, else None.

Zephyr prefixes every line with a timestamp and module tag, so both
patterns are searched for rather than anchored.

**called by** `main`

### `discovery_payloads(node: str) -> list[tuple[str, dict]]`
`integration/homeassistant/aliro_mqtt_bridge.py:60`

Return (topic, config) pairs announcing both entities to Home Assistant.

**called by** `main`

### `reading_to_message(node: str, reading: dict) -> tuple[str, str]`
`integration/homeassistant/aliro_mqtt_bridge.py:97`

Map a parsed reading to the (topic, payload) that carries it.

**called by** `main`

### `open_lines(port: str, baud: int) -> Iterator[str]`
`integration/homeassistant/aliro_mqtt_bridge.py:105`

Yield console lines from stdin ('-') or from a serial port.

**called by** `main`

### `main() -> int`
`integration/homeassistant/aliro_mqtt_bridge.py:120`

Parse a serial line or read lines from stdin ('-'), decode range and access readings, publish to MQTT (or stdout in dry-run mode), and notify Home Assistant via discovery payloads; return 0 on clean exit or KeyboardInterrupt.

**calls** `discovery_payloads`, `open_lines`, `parse_line`, `reading_to_message`
