<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/config_flow.py`

Manual direct-serial config flow for the HA=1 OpenAliro beta.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](const.md)

## API

### `class OpenAliroConfigFlow(config_entries.ConfigFlow)`
`integration/homeassistant/custom_components/openaliro/config_flow.py:15`

Ask for a serial port, verify it, and retain a hashed USB identity.

#### `OpenAliroConfigFlow.async_step_user(self, user_input: dict[str, object] | None=None) -> FlowResult`
`integration/homeassistant/custom_components/openaliro/config_flow.py:20`

Handle the initial user config flow step: discover serial ports, probe the device, deduplicate by identity, create the config entry with device ID and connection parameters.

#### `OpenAliroConfigFlow.async_step_reconfigure(self, user_input: dict[str, object] | None=None) -> FlowResult`
`integration/homeassistant/custom_components/openaliro/config_flow.py:63`

Replace only the selected interface after a USB port change.
