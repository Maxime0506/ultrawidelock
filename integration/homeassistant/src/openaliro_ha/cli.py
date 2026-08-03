"""Small, non-interactive HA=1 staging CLI for safe offline operations."""

import argparse
import asyncio
import json
import re
from dataclasses import asdict
from pathlib import Path
from typing import Optional, Sequence

from . import __version__
from .agent import AgentError, doctor as run_doctor, probe_device, run as run_agent
from .config import AgentConfig, ConfigError, DeviceConfig, MqttConfig, load_config, write_config
from .models import AccessEvent, DistanceReading, Observation
from .parser import parse_console_line
from .serial_session import SessionState
from .serial_transport import SerialPort, discover_serial_ports


CONSOLE_SCHEMA = "streaming-v1"
MAX_REPLAY_BYTES = 1024 * 1024
READY_STATES = frozenset({SessionState.READY_STREAMING, SessionState.READY_COMPATIBILITY})
DEVICE_PATH_RE = re.compile(r"/dev/\S+")


def _observation_dict(observation: Observation) -> dict[str, object]:
    """Convert an Observation union (DistanceReading or AccessEvent) to a dict with a kind field and all dataclass fields. Raises TypeError if the observation is not a recognized type."""
    if isinstance(observation, DistanceReading):
        return {"kind": "range", **asdict(observation)}
    if isinstance(observation, AccessEvent):
        return {"kind": "access", **asdict(observation)}
    raise TypeError("unsupported observation")


def _read_capture(path: Path) -> list[Observation]:
    """Read a UTF-8 sanitized capture file, parse it line by line into Observation objects, and raise ValueError if the file cannot be read, exceeds MAX_REPLAY_BYTES, or contains invalid UTF-8."""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError("capture cannot be read") from error
    if len(data) > MAX_REPLAY_BYTES:
        raise ValueError("capture exceeds the size limit")
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("capture is not valid UTF-8") from error
    return [observation for line in lines if (observation := parse_console_line(line))]


def _parser() -> argparse.ArgumentParser:
    """Build an argument parser for the openaliro-ha CLI tool with subcommands for version, replay, doctor, configure, and run; the configure subcommand accepts optional device discovery and MQTT configuration flags."""
    parser = argparse.ArgumentParser(prog="openaliro-ha")
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("openaliro-ha.toml"),
        help="agent TOML configuration path (default: ./openaliro-ha.toml)",
    )
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("version", help="print agent and supported console-schema versions")
    replay = subcommands.add_parser(
        "replay",
        help="parse a sanitized capture without a board or broker",
    )
    replay.add_argument("capture", type=Path)
    replay.add_argument("--json", action="store_true", help="emit a stable JSON array")
    doctor = subcommands.add_parser("doctor", help="check config, console, MQTT, and TLS")
    doctor.add_argument("--json", action="store_true", help="emit stable redacted JSON")
    configure = subcommands.add_parser(
        "configure", help="discover one console and create or update config"
    )
    configure.add_argument("--device-id", help="skip the device prompt")
    configure.add_argument("--mqtt-host", help="configure MQTT from flags instead of prompts")
    configure.add_argument("--mqtt-port", type=int, default=8883)
    configure.add_argument("--mqtt-username")
    configure.add_argument("--mqtt-password-file", help="file holding the MQTT password")
    configure.add_argument("--mqtt-ca", help="TLS CA certificate path")
    configure.add_argument(
        "--allow-anonymous",
        action="store_true",
        help="configure an unauthenticated broker connection",
    )
    subcommands.add_parser("run", help="run the long-lived MQTT agent")
    return parser


def _prompt(input_fn: object, message: str, *, default: str | None = None) -> str:
    """Return the user's input after stripping whitespace, or the default value if the input is empty and a default was provided."""
    value = input_fn(message)  # type: ignore[operator]
    value = value.strip()
    return default if not value and default is not None else value


def _candidate_label(index: int, port: SerialPort) -> str:
    """Describe a candidate without echoing its device path or USB serial."""

    product = port.product or "unknown product"
    interface = port.interface or "unknown interface"
    vid_pid = (
        f"{port.vid:04x}:{port.pid:04x}" if port.vid is not None and port.pid is not None else "unknown"
    )
    return f"{index}: {product}, interface {interface}, USB {vid_pid}"


def _probe_candidate(port: SerialPort) -> Optional[str]:
    """Return None when a candidate answers, else why it did not.

    The reason is stripped of device paths so the picker keeps hiding them.
    """

    probe = DeviceConfig(device_id="probe", serial_port=port.device, serial_identity=port.identity)
    try:
        state = asyncio.run(probe_device(probe))
    except (AgentError, ConfigError) as error:
        cause = error.__cause__
        reason = f"{error} ({cause})" if cause is not None else str(error)
        return DEVICE_PATH_RE.sub("the port", reason)
    return None if state in READY_STATES else f"reached {state.value}"


def _select_port(input_fn: object, output_fn: object) -> SerialPort:
    """Probe all discoverable serial interfaces with USB identity and select one for the OpenAliro console. List outcomes (ready or probe failure reason), auto-select if exactly one is ready, otherwise prompt for index. Raises ConfigError if no candidates exist or selection is invalid."""
    candidates = [port for port in discover_serial_ports() if port.identity is not None]
    if not candidates:
        raise ConfigError("no serial interfaces with stable USB identity were found")
    output_fn(f"Probing {len(candidates)} serial interfaces...")  # type: ignore[operator]
    labels = []
    ready = []
    for index, port in enumerate(candidates, start=1):
        reason = _probe_candidate(port)
        outcome = f"no response: {reason}" if reason is not None else "ready"
        labels.append(f"{_candidate_label(index, port)} -> {outcome}")
        output_fn(f"  {labels[-1]}")  # type: ignore[operator]
        if reason is None:
            ready.append((index, port))
    if len(ready) == 1:
        index, port = ready[0]
        output_fn(f"Selected interface {index}.")  # type: ignore[operator]
        return port
    output_fn("Select the OpenAliro console interface:")  # type: ignore[operator]
    for label in labels:
        output_fn(label)  # type: ignore[operator]
    try:
        selected = int(_prompt(input_fn, "Interface number: "))
    except ValueError as error:
        raise ConfigError("serial interface selection must be a number") from error
    if not 1 <= selected <= len(candidates):
        raise ConfigError("serial interface selection is unavailable")
    return candidates[selected - 1]


def _new_mqtt_config(input_fn: object) -> MqttConfig:
    """Interactively prompt for MQTT configuration: host, TLS port, CA certificate path, username. If username is given, prompt for password environment variable. If not, require explicit "ALLOW ANONYMOUS MQTT" confirmation. Return a populated MqttConfig struct."""
    host = _prompt(input_fn, "MQTT host: ")
    if not host:
        raise ConfigError("MQTT host is required")
    try:
        port = int(_prompt(input_fn, "MQTT TLS port [8883]: ", default="8883"))
    except ValueError as error:
        raise ConfigError("MQTT TLS port must be a number") from error
    if not 1 <= port <= 65535:
        raise ConfigError("MQTT TLS port must be between 1 and 65535")
    ca_path = _prompt(
        input_fn,
        "TLS CA certificate path (leave empty only for a system-trusted broker): ",
    )
    username = _prompt(input_fn, "MQTT username (leave empty only for explicit anonymous MQTT): ")
    if username:
        password_env = _prompt(
            input_fn,
            "MQTT password environment variable [OPENALIRO_HA_MQTT_PASSWORD]: ",
            default="OPENALIRO_HA_MQTT_PASSWORD",
        )
        return MqttConfig(
            host=host,
            port=port,
            username=username,
            password_env=password_env,
            ca_path=ca_path or None,
        )
    confirmation = _prompt(input_fn, "Type ALLOW ANONYMOUS MQTT to continue: ")
    if confirmation != "ALLOW ANONYMOUS MQTT":
        raise ConfigError("anonymous MQTT requires explicit confirmation")
    return MqttConfig(host=host, port=port, ca_path=ca_path or None, allow_anonymous=True)


def _flag(arguments: argparse.Namespace, name: str) -> Optional[object]:
    """Read an optional configure flag, tolerating callers that omit it."""

    return getattr(arguments, name, None)


def _flag_mqtt_config(arguments: argparse.Namespace) -> MqttConfig:
    """Build an MQTT config from flags so a setup script needs no prompts."""

    username = _flag(arguments, "mqtt_username")
    password_file = _flag(arguments, "mqtt_password_file")
    if username and not password_file:
        raise ConfigError("--mqtt-username requires --mqtt-password-file")
    # The prompted path gates anonymous MQTT behind a typed confirmation. Without
    # an equivalent here, one forgotten --mqtt-username would quietly write an
    # unauthenticated broker connection.
    if not username and not _flag(arguments, "allow_anonymous"):
        raise ConfigError("--mqtt-username is required unless --allow-anonymous is given")
    port = _flag(arguments, "mqtt_port") or 8883
    if not 1 <= int(port) <= 65535:
        raise ConfigError("MQTT TLS port must be between 1 and 65535")
    return MqttConfig(
        host=str(_flag(arguments, "mqtt_host")),
        port=int(port),
        username=username,
        password_file=password_file,
        ca_path=_flag(arguments, "mqtt_ca") or None,
        allow_anonymous=not username,
    )


def _configure(arguments: argparse.Namespace, *, input_fn: object = input, output_fn: object = print) -> int:
    """Create or add one device, from flags when given and prompts otherwise."""

    try:
        existing = load_config(arguments.config) if arguments.config.exists() else None
        port = _select_port(input_fn, output_fn)
        device_id = _flag(arguments, "device_id") or _prompt(
            input_fn, "Device ID [front-door]: ", default="front-door"
        )
        selected = DeviceConfig(
            device_id=device_id,
            serial_port=port.device,
            serial_identity=port.identity,
        )
        state = asyncio.run(probe_device(selected))
        if state not in READY_STATES:
            raise ConfigError("selected serial interface did not reach a ready state")
        if _flag(arguments, "mqtt_host"):
            mqtt = _flag_mqtt_config(arguments)
        elif existing is not None:
            mqtt = existing.mqtt
        else:
            mqtt = _new_mqtt_config(input_fn)
        old_devices = existing.devices if existing is not None else ()
        persisted = DeviceConfig(
            device_id=device_id,
            serial_port="auto",
            serial_identity=port.identity,
            baud=selected.baud,
        )
        retained_devices = tuple(device for device in old_devices if device.device_id != device_id)
        write_config(arguments.config, AgentConfig(mqtt=mqtt, devices=(*retained_devices, persisted)))
    except (AgentError, ConfigError) as error:
        raise error
    output_fn("configuration saved")  # type: ignore[operator]
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """Run an HA=1-gated offline command without exposing raw capture text."""

    parser = _parser()
    arguments = parser.parse_args(argv)
    if arguments.command == "version":
        print(f"openaliro-ha {__version__} (console schema: {CONSOLE_SCHEMA})")
        return 0
    if arguments.command == "configure":
        try:
            return _configure(arguments)
        except (AgentError, ConfigError) as error:
            parser.error(str(error))
    if arguments.command in {"doctor", "run"}:
        try:
            config = load_config(arguments.config)
            if arguments.command == "doctor":
                results = asyncio.run(run_doctor(config))
            else:
                asyncio.run(run_agent(config))
                return 0
        except (AgentError, ConfigError) as error:
            parser.error(str(error))
        except KeyboardInterrupt:
            return 0
        if arguments.json:
            print(
                json.dumps(
                    {
                        "ok": True,
                        "devices": [
                            {"device_id": result.device_id, "serial_state": result.serial_state}
                            for result in results
                        ],
                    },
                    sort_keys=True,
                )
            )
        else:
            print("doctor: all checks passed")
        return 0
    try:
        observations = _read_capture(arguments.capture)
    except ValueError as error:
        parser.error(str(error))
    rendered = [_observation_dict(observation) for observation in observations]
    if arguments.json:
        print(json.dumps(rendered, sort_keys=True))
    else:
        for observation in rendered:
            print(json.dumps(observation, sort_keys=True))
    return 0
