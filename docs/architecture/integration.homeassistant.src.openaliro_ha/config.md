<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/config.py`

Versioned, secret-free TOML configuration for the HA=1-only agent.

**used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](mqtt.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class ConfigError(ValueError)`
`integration/homeassistant/src/openaliro_ha/config.py:19`

A configuration error safe to present without echoing configuration values.

**called by** `_boolean`, `_integer`, `_mapping`, `_parse_device`, `_parse_mqtt`, `_reject_unknown`, `_string`, `config_from_mapping`

### `class MqttConfig`
`integration/homeassistant/src/openaliro_ha/config.py:24`

Broker connection settings with a secret reference, never a secret value.

**called by** `_parse_mqtt`

### `class DeviceConfig`
`integration/homeassistant/src/openaliro_ha/config.py:41`

One stable lock identifier and its serial connection preferences.

**called by** `_parse_device`

### `class AgentConfig`
`integration/homeassistant/src/openaliro_ha/config.py:52`

The first version of the standalone-agent configuration contract.

**called by** `config_from_mapping`

### `_mapping(value: Any, field: str) -> Mapping[str, Any]`
`integration/homeassistant/src/openaliro_ha/config.py:60`

Coerce value to a dict; raise ConfigError if not a table (Mapping).

**called by** `config_from_mapping`  ·  **calls** `ConfigError`

### `_string(value: Any, field: str, *, optional: bool=False) -> Optional[str]`
`integration/homeassistant/src/openaliro_ha/config.py:67`

Coerce value to a non-empty string, returning None if optional and value is None; raise ConfigError otherwise.

**called by** `_parse_device`, `_parse_mqtt`  ·  **calls** `ConfigError`

### `_boolean(value: Any, field: str, *, default: bool=False) -> bool`
`integration/homeassistant/src/openaliro_ha/config.py:76`

Coerce value to bool, returning default if None; raise ConfigError if not a bool.

**called by** `_parse_mqtt`  ·  **calls** `ConfigError`

### `_integer(value: Any, field: str, *, default: int) -> int`
`integration/homeassistant/src/openaliro_ha/config.py:85`

Coerce value to int, returning default if None; raise ConfigError if not an int or is a bool.

**called by** `_parse_device`, `_parse_mqtt`, `config_from_mapping`  ·  **calls** `ConfigError`

### `_reject_unknown(values: Mapping[str, Any], allowed: set[str], field: str) -> None`
`integration/homeassistant/src/openaliro_ha/config.py:94`

Raise ConfigError if values contains any keys not in allowed.

**called by** `_parse_device`, `_parse_mqtt`, `config_from_mapping`  ·  **calls** `ConfigError`

### `_parse_mqtt(values: Mapping[str, Any]) -> MqttConfig`
`integration/homeassistant/src/openaliro_ha/config.py:101`

Validate and parse the MQTT configuration block, checking all keys are recognized, validating port and password sources, enforcing mutual exclusivity of authentication methods, and returning an MqttConfig struct.

**called by** `config_from_mapping`  ·  **calls** `ConfigError`, `MqttConfig`, `_boolean`, `_integer`, `_reject_unknown`, `_string`

### `_parse_device(device_id: str, values: Mapping[str, Any]) -> DeviceConfig`
`integration/homeassistant/src/openaliro_ha/config.py:162`

Validate and parse one device configuration block, checking device ID format, rejecting unknown keys, validating serial port and baud rate ranges, and returning a DeviceConfig struct.

**called by** `config_from_mapping`  ·  **calls** `ConfigError`, `DeviceConfig`, `_integer`, `_reject_unknown`, `_string`

### `config_from_mapping(values: Mapping[str, Any]) -> AgentConfig`
`integration/homeassistant/src/openaliro_ha/config.py:188`

Validate a parsed TOML mapping without retaining raw secret values.

**called by** `load_config`, `serialize_config`  ·  **calls** `AgentConfig`, `ConfigError`, `_integer`, `_mapping`, `_parse_device`, `_parse_mqtt`, `_reject_unknown`

### `load_config(path: Path) -> AgentConfig`
`integration/homeassistant/src/openaliro_ha/config.py:206`

Load a bounded TOML file and return validated, secret-free settings.

**calls** `ConfigError`, `config_from_mapping`

### `_toml_string(value: str) -> str`
`integration/homeassistant/src/openaliro_ha/config.py:222`

JSON-encode string with ensure_ascii=False for TOML serialization.

**called by** `serialize_config`

### `serialize_config(config: AgentConfig) -> str`
`integration/homeassistant/src/openaliro_ha/config.py:227`

Serialize validated configuration without resolving any secret reference.

**called by** `write_config`  ·  **calls** `ConfigError`, `_toml_string`, `config_from_mapping`

### `write_config(path: Path, config: AgentConfig) -> None`
`integration/homeassistant/src/openaliro_ha/config.py:286`

Atomically write configuration with user-only permissions where supported.

**calls** `ConfigError`, `serialize_config`

### `redacted_config(config: AgentConfig) -> dict[str, Any]`
`integration/homeassistant/src/openaliro_ha/config.py:310`

Return diagnostics-safe configuration without secret references or device paths.
