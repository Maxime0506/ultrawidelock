<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/__init__.py`

HA=1-only OpenAliro direct-serial integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](runtime.md)

## API

### `async_setup_entry(hass: HomeAssistant, entry: OpenAliroConfigEntry) -> bool`
`integration/homeassistant/custom_components/openaliro/__init__.py:34`

Set up direct serial ownership for one config entry.

### `async_unload_entry(hass: HomeAssistant, entry: OpenAliroConfigEntry) -> bool`
`integration/homeassistant/custom_components/openaliro/__init__.py:56`

Unload platforms and release the exclusive serial port.
