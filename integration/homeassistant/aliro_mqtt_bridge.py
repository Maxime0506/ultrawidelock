#!/usr/bin/env python3
"""Republish the lock's console log to MQTT as Home Assistant entities.

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
"""

import argparse
import json
import re
import sys
from typing import Iterator, Optional

# The firmware format string is "rng  blk=%-3u d=%dmm  tof=%d"
# (modules/woz_uwb/src/ccc/ccc_shim_rx.c:498). The %-3u pad and the literal
# double spaces mean the runs of whitespace vary, so match \s+ rather than ' '.
RNG_RE = re.compile(r"rng\s+blk=(\d+)\s+d=(-?\d+)mm\s+tof=(-?\d+)")

# The grant/deny console lines, emitted once per completed transaction.
ACCESS_RE = re.compile(r"ACCESS (GRANTED|DENIED)")


def parse_line(line: str) -> Optional[dict]:
    """Return a reading dict for a range or access line, else None.

    Zephyr prefixes every line with a timestamp and module tag, so both
    patterns are searched for rather than anchored.
    """
    m = RNG_RE.search(line)
    if m:
        return {
            "kind": "range",
            "block": int(m.group(1)),
            "distance_mm": int(m.group(2)),
            "tof": int(m.group(3)),
        }

    m = ACCESS_RE.search(line)
    if m:
        return {"kind": "access", "verdict": m.group(1).lower()}

    return None


def discovery_payloads(node: str) -> list[tuple[str, dict]]:
    """Return (topic, config) pairs announcing both entities to Home Assistant."""
    device = {
        "identifiers": [node],
        "name": node,
        "manufacturer": "openaliro",
        "model": "nRF5340 Aliro lock",
    }
    base = f"aliro/{node}"
    return [
        (
            f"homeassistant/sensor/{node}/distance/config",
            {
                "name": "Distance",
                "unique_id": f"{node}_distance",
                "state_topic": f"{base}/distance",
                "availability_topic": f"{base}/status",
                "unit_of_measurement": "mm",
                "device_class": "distance",
                "state_class": "measurement",
                "device": device,
            },
        ),
        (
            f"homeassistant/event/{node}/access/config",
            {
                "name": "Access",
                "unique_id": f"{node}_access",
                "state_topic": f"{base}/access",
                "availability_topic": f"{base}/status",
                "event_types": ["granted", "denied"],
                "device": device,
            },
        ),
    ]


def reading_to_message(node: str, reading: dict) -> tuple[str, str]:
    """Map a parsed reading to the (topic, payload) that carries it."""
    base = f"aliro/{node}"
    if reading["kind"] == "range":
        return f"{base}/distance", str(reading["distance_mm"])
    return f"{base}/access", json.dumps({"event_type": reading["verdict"]})


def open_lines(port: str, baud: int) -> Iterator[str]:
    """Yield console lines from stdin ('-') or from a serial port."""
    if port == "-":
        yield from sys.stdin
        return

    import serial  # pyserial, only needed for a real port

    with serial.Serial(port, baud, timeout=1) as ser:
        while True:
            raw = ser.readline()
            if raw:
                yield raw.decode("utf-8", errors="replace")


def main() -> int:
    """Parse a serial line or read lines from stdin ('-'), decode range and access readings, publish to MQTT (or stdout in dry-run mode), and notify Home Assistant via discovery payloads; return 0 on clean exit or KeyboardInterrupt."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True, help="serial device, or '-' for stdin")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--broker", default="localhost")
    ap.add_argument("--broker-port", type=int, default=1883)
    ap.add_argument("--node", default="aliro-lock", help="MQTT/entity node id")
    ap.add_argument(
        "--dry-run",
        action="store_true",
        help="print topic/payload instead of connecting to a broker",
    )
    args = ap.parse_args()

    base = f"aliro/{args.node}"

    if args.dry_run:
        client = None
        for topic, config in discovery_payloads(args.node):
            print(f"{topic} {json.dumps(config)}")
    else:
        import paho.mqtt.client as mqtt

        client = mqtt.Client()
        client.will_set(f"{base}/status", "offline", retain=True)
        client.connect(args.broker, args.broker_port)
        client.loop_start()
        for topic, config in discovery_payloads(args.node):
            client.publish(topic, json.dumps(config), retain=True)
        client.publish(f"{base}/status", "online", retain=True)

    try:
        for line in open_lines(args.port, args.baud):
            reading = parse_line(line)
            if reading is None:
                continue
            topic, payload = reading_to_message(args.node, reading)
            if client is None:
                print(f"{topic} {payload}")
            else:
                client.publish(topic, payload)
    except KeyboardInterrupt:
        pass
    finally:
        if client is not None:
            client.publish(f"{base}/status", "offline", retain=True)
            client.loop_stop()
            client.disconnect()

    return 0


if __name__ == "__main__":
    sys.exit(main())
