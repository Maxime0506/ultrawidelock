<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/config.py`

Versioned, secret-free TOML configuration for the HA=1-only agent.

**used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](mqtt.md)

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

### `config_from_mapping(values: Mapping[str, Any]) -> AgentConfig`
`integration/homeassistant/src/openaliro_ha/config.py:181`

Validate a parsed TOML mapping without retaining raw secret values.

**called by** `load_config`, `serialize_config`  ·  **calls** `AgentConfig`, `ConfigError`, `_integer`, `_mapping`, `_parse_device`, `_parse_mqtt`, `_reject_unknown`

### `load_config(path: Path) -> AgentConfig`
`integration/homeassistant/src/openaliro_ha/config.py:199`

Load a bounded TOML file and return validated, secret-free settings.

**calls** `ConfigError`, `config_from_mapping`

### `serialize_config(config: AgentConfig) -> str`
`integration/homeassistant/src/openaliro_ha/config.py:219`

Serialize validated configuration without resolving any secret reference.

**called by** `write_config`  ·  **calls** `ConfigError`, `_toml_string`, `config_from_mapping`

### `write_config(path: Path, config: AgentConfig) -> None`
`integration/homeassistant/src/openaliro_ha/config.py:278`

Atomically write configuration with user-only permissions where supported.

**calls** `ConfigError`, `serialize_config`

### `redacted_config(config: AgentConfig) -> dict[str, Any]`
`integration/homeassistant/src/openaliro_ha/config.py:302`

Return diagnostics-safe configuration without secret references or device paths.

<details><summary>Undocumented (8)</summary>

- `_mapping`
- `_string`
- `_boolean`
- `_integer`
- `_reject_unknown`
- `_parse_mqtt`
- `_parse_device`
- `_toml_string`

</details>
