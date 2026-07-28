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

<details><summary>Undocumented (6)</summary>

- `OpenAliroAccessEvent.__init__`
- `OpenAliroAccessEvent.available`
- `OpenAliroAccessEvent.device_info`
- `OpenAliroAccessEvent.async_added_to_hass`
- `OpenAliroAccessEvent.update`
- `OpenAliroAccessEvent.async_will_remove_from_hass`

</details>
