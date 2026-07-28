"""Versioned, secret-free TOML configuration for the HA=1-only agent."""

import json
import os
import re
import tempfile
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, Mapping, Optional


SCHEMA_VERSION = 1
MAX_CONFIG_BYTES = 1024 * 1024
DEVICE_ID_RE = re.compile(r"[a-z0-9][a-z0-9_-]{0,63}\Z")
ENV_NAME_RE = re.compile(r"[A-Z_][A-Z0-9_]*\Z")


class ConfigError(ValueError):
    """A configuration error safe to present without echoing configuration values."""


@dataclass(frozen=True)
class MqttConfig:
    """Broker connection settings with a secret reference, never a secret value."""

    host: str
    port: int = 8883
    tls: bool = True
    username: Optional[str] = None
    password_env: Optional[str] = None
    password_file: Optional[str] = None
    ca_path: Optional[str] = None
    client_cert: Optional[str] = None
    client_key: Optional[str] = None
    allow_insecure: bool = False
    allow_anonymous: bool = False


@dataclass(frozen=True)
class DeviceConfig:
    """One stable lock identifier and its serial connection preferences."""

    device_id: str
    serial_port: str = "auto"
    serial_identity: Optional[str] = None
    baud: int = 115200
    distance_mode: Literal["auto", "streaming", "compatibility"] = "auto"


@dataclass(frozen=True)
class AgentConfig:
    """The first version of the standalone-agent configuration contract."""

    mqtt: MqttConfig
    devices: tuple[DeviceConfig, ...]
    schema_version: int = SCHEMA_VERSION


def _mapping(value: Any, field: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ConfigError(f"{field} must be a table")
    return value


def _string(value: Any, field: str, *, optional: bool = False) -> Optional[str]:
    if value is None and optional:
        return None
    if not isinstance(value, str) or not value:
        raise ConfigError(f"{field} must be a non-empty string")
    return value


def _boolean(value: Any, field: str, *, default: bool = False) -> bool:
    if value is None:
        return default
    if not isinstance(value, bool):
        raise ConfigError(f"{field} must be true or false")
    return value


def _integer(value: Any, field: str, *, default: int) -> int:
    if value is None:
        return default
    if not isinstance(value, int) or isinstance(value, bool):
        raise ConfigError(f"{field} must be an integer")
    return value


def _reject_unknown(values: Mapping[str, Any], allowed: set[str], field: str) -> None:
    unexpected = set(values) - allowed
    if unexpected:
        raise ConfigError(f"{field} contains unsupported keys")


def _parse_mqtt(values: Mapping[str, Any]) -> MqttConfig:
    allowed = {
        "host",
        "port",
        "tls",
        "username",
        "password_env",
        "password_file",
        "ca_path",
        "client_cert",
        "client_key",
        "allow_insecure",
        "allow_anonymous",
    }
    _reject_unknown(values, allowed, "mqtt")
    host = _string(values.get("host"), "mqtt.host")
    port = _integer(values.get("port"), "mqtt.port", default=8883)
    if not 1 <= port <= 65535:
        raise ConfigError("mqtt.port must be between 1 and 65535")
    tls = _boolean(values.get("tls"), "mqtt.tls", default=True)
    username = _string(values.get("username"), "mqtt.username", optional=True)
    password_env = _string(values.get("password_env"), "mqtt.password_env", optional=True)
    password_file = _string(values.get("password_file"), "mqtt.password_file", optional=True)
    ca_path = _string(values.get("ca_path"), "mqtt.ca_path", optional=True)
    client_cert = _string(values.get("client_cert"), "mqtt.client_cert", optional=True)
    client_key = _string(values.get("client_key"), "mqtt.client_key", optional=True)
    allow_insecure = _boolean(values.get("allow_insecure"), "mqtt.allow_insecure")
    allow_anonymous = _boolean(values.get("allow_anonymous"), "mqtt.allow_anonymous")

    if password_env and password_file:
        raise ConfigError("mqtt password source must be configured once")
    if password_env and not ENV_NAME_RE.fullmatch(password_env):
        raise ConfigError("mqtt.password_env must be an environment variable name")
    if username and not (password_env or password_file):
        raise ConfigError("mqtt.username requires a password source")
    if bool(client_cert) != bool(client_key):
        raise ConfigError("mqtt client certificate and key must be configured together")
    if not tls:
        if not allow_insecure:
            raise ConfigError("mqtt.tls=false requires mqtt.allow_insecure=true")
        if ca_path or client_cert or client_key:
            raise ConfigError("mqtt TLS certificate settings require mqtt.tls=true")
    if not (username or client_cert or allow_anonymous):
        raise ConfigError("mqtt requires authentication or mqtt.allow_anonymous=true")

    return MqttConfig(
        host=host,
        port=port,
        tls=tls,
        username=username,
        password_env=password_env,
        password_file=password_file,
        ca_path=ca_path,
        client_cert=client_cert,
        client_key=client_key,
        allow_insecure=allow_insecure,
        allow_anonymous=allow_anonymous,
    )


def _parse_device(device_id: str, values: Mapping[str, Any]) -> DeviceConfig:
    if not DEVICE_ID_RE.fullmatch(device_id):
        raise ConfigError("device ID must contain only lowercase letters, numbers, _ or -")
    _reject_unknown(values, {"serial_port", "serial_identity", "baud", "distance_mode"}, "device")
    serial_port = _string(values.get("serial_port", "auto"), "device.serial_port")
    serial_identity = _string(values.get("serial_identity"), "device.serial_identity", optional=True)
    if serial_identity is not None and not re.fullmatch(r"[0-9a-f]{24}", serial_identity):
        raise ConfigError("device.serial_identity is invalid")
    if serial_port == "auto" and serial_identity is None:
        raise ConfigError("device.serial_port=auto requires device.serial_identity")
    baud = _integer(values.get("baud"), "device.baud", default=115200)
    if not 1200 <= baud <= 4_000_000:
        raise ConfigError("device.baud is outside the supported range")
    distance_mode = values.get("distance_mode", "auto")
    if distance_mode not in {"auto", "streaming", "compatibility"}:
        raise ConfigError("device.distance_mode is unsupported")
    return DeviceConfig(
        device_id=device_id,
        serial_port=serial_port,
        serial_identity=serial_identity,
        baud=baud,
        distance_mode=distance_mode,
    )


def config_from_mapping(values: Mapping[str, Any]) -> AgentConfig:
    """Validate a parsed TOML mapping without retaining raw secret values."""

    _reject_unknown(values, {"schema_version", "mqtt", "devices"}, "config")
    schema_version = _integer(values.get("schema_version"), "schema_version", default=0)
    if schema_version != SCHEMA_VERSION:
        raise ConfigError("unsupported schema_version")
    mqtt = _parse_mqtt(_mapping(values.get("mqtt"), "mqtt"))
    raw_devices = _mapping(values.get("devices"), "devices")
    if not raw_devices:
        raise ConfigError("devices must contain at least one device")
    devices = tuple(
        _parse_device(device_id, _mapping(device_values, "device"))
        for device_id, device_values in sorted(raw_devices.items())
    )
    return AgentConfig(mqtt=mqtt, devices=devices, schema_version=schema_version)


def load_config(path: Path) -> AgentConfig:
    """Load a bounded TOML file and return validated, secret-free settings."""

    try:
        data = path.read_bytes()
    except OSError as error:
        raise ConfigError("configuration file cannot be read") from error
    if len(data) > MAX_CONFIG_BYTES:
        raise ConfigError("configuration file exceeds the size limit")
    try:
        values = tomllib.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise ConfigError("configuration file is not valid UTF-8 TOML") from error
    return config_from_mapping(values)


def _toml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def serialize_config(config: AgentConfig) -> str:
    """Serialize validated configuration without resolving any secret reference."""

    if config.schema_version != SCHEMA_VERSION:
        raise ConfigError("unsupported schema_version")
    mqtt = config.mqtt
    mqtt_values = {
        "host": mqtt.host,
        "port": mqtt.port,
        "tls": mqtt.tls,
        "username": mqtt.username,
        "password_env": mqtt.password_env,
        "password_file": mqtt.password_file,
        "ca_path": mqtt.ca_path,
        "client_cert": mqtt.client_cert,
        "client_key": mqtt.client_key,
        "allow_insecure": mqtt.allow_insecure,
        "allow_anonymous": mqtt.allow_anonymous,
    }
    config_from_mapping(
        {
            "schema_version": config.schema_version,
            "mqtt": {key: value for key, value in mqtt_values.items() if value is not None},
            "devices": {
                device.device_id: {
                    "serial_port": device.serial_port,
                    "serial_identity": device.serial_identity,
                    "baud": device.baud,
                    "distance_mode": device.distance_mode,
                }
                for device in config.devices
            },
        }
    )

    lines = [f"schema_version = {config.schema_version}", "", "[mqtt]"]
    for key, value in mqtt_values.items():
        if value is None:
            continue
        rendered = _toml_string(value) if isinstance(value, str) else str(value).lower()
        lines.append(f"{key} = {rendered}")
    for device in config.devices:
        lines.extend(
            [
                "",
                f"[devices.{device.device_id}]",
                f"serial_port = {_toml_string(device.serial_port)}",
                *(
                    [f"serial_identity = {_toml_string(device.serial_identity)}"]
                    if device.serial_identity is not None
                    else []
                ),
                f"baud = {device.baud}",
                f"distance_mode = {_toml_string(device.distance_mode)}",
            ]
        )
    return "\n".join(lines) + "\n"


def write_config(path: Path, config: AgentConfig) -> None:
    """Atomically write configuration with user-only permissions where supported."""

    rendered = serialize_config(config)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_path = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as config_file:
            descriptor = -1
            config_file.write(rendered)
        os.replace(temporary_path, path)
        os.chmod(path, 0o600)
    except OSError as error:
        raise ConfigError("configuration file cannot be written") from error
    finally:
        if descriptor != -1:
            os.close(descriptor)
        try:
            Path(temporary_path).unlink()
        except FileNotFoundError:
            pass


def redacted_config(config: AgentConfig) -> dict[str, Any]:
    """Return diagnostics-safe configuration without secret references or device paths."""

    mqtt = config.mqtt
    return {
        "schema_version": config.schema_version,
        "mqtt": {
            "host": "<redacted>",
            "host_configured": bool(mqtt.host),
            "port": mqtt.port,
            "tls": mqtt.tls,
            "username_configured": mqtt.username is not None,
            "password_source_configured": bool(mqtt.password_env or mqtt.password_file),
            "ca_configured": mqtt.ca_path is not None,
            "client_certificate_configured": mqtt.client_cert is not None,
            "allow_insecure": mqtt.allow_insecure,
            "allow_anonymous": mqtt.allow_anonymous,
        },
        "devices": [
            {
                "device_id": device.device_id,
                "serial_port": "auto" if device.serial_port == "auto" else "<redacted>",
                "serial_identity_configured": device.serial_identity is not None,
                "baud": device.baud,
                "distance_mode": device.distance_mode,
            }
            for device in config.devices
        ],
    }
