<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/event.py`

Access outcome event entity for the HA=1 OpenAliro direct integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](runtime.md)

## API

### `async_setup_entry(hass: HomeAssistant, entry: ConfigEntry[OpenAliroRuntime], async_add_entities: AddEntitiesCallback) -> None`
`integration/homeassistant/custom_components/openaliro/event.py:13`

Create the credential-free access event entity.

**calls** `OpenAliroAccessEvent`

### `class OpenAliroAccessEvent(EventEntity)`
`integration/homeassistant/custom_components/openaliro/event.py:21`

Emit exactly the granted and denied outcomes parsed by the shared library.

**called by** `async_setup_entry`

#### `OpenAliroAccessEvent.__init__(self, runtime: OpenAliroRuntime) -> None`
`integration/homeassistant/custom_components/openaliro/event.py:29`

Initialize the access event entity with the runtime reference and a unique ID derived from the device ID.

#### `OpenAliroAccessEvent.available(self) -> bool`
`integration/homeassistant/custom_components/openaliro/event.py:36`

Return the runtime availability (true if the connection is up).

#### `OpenAliroAccessEvent.device_info(self) -> DeviceInfo`
`integration/homeassistant/custom_components/openaliro/event.py:41`

Return device metadata linking this entity to the OpenAliro device.

#### `OpenAliroAccessEvent.async_added_to_hass(self) -> None`
`integration/homeassistant/custom_components/openaliro/event.py:45`

Register a listener callback that publishes the last access verdict as a Home Assistant event when the runtime changes state.

#### `OpenAliroAccessEvent.async_will_remove_from_hass(self) -> None`
`integration/homeassistant/custom_components/openaliro/event.py:57`

Unregister the listener callback on entity removal.

<details><summary>Undocumented (1)</summary>

- `OpenAliroAccessEvent.update`

</details>
