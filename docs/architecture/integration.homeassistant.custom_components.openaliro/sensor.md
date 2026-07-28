<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/sensor.py`

Distance sensor for the HA=1 OpenAliro direct integration.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](const.md), [`integration/homeassistant/custom_components/openaliro/runtime.py`](runtime.md)

## API

### `async_setup_entry(hass: HomeAssistant, entry: ConfigEntry[OpenAliroRuntime], async_add_entities: AddEntitiesCallback) -> None`
`integration/homeassistant/custom_components/openaliro/sensor.py:14`

Create the one supported distance entity.

**calls** `OpenAliroDistanceSensor`

### `class OpenAliroDistanceSensor(SensorEntity)`
`integration/homeassistant/custom_components/openaliro/sensor.py:22`

Latest valid UWB range, without peer or credential metadata.

**called by** `async_setup_entry`

<details><summary>Undocumented (7)</summary>

- `OpenAliroDistanceSensor.__init__`
- `OpenAliroDistanceSensor.available`
- `OpenAliroDistanceSensor.native_value`
- `OpenAliroDistanceSensor.device_info`
- `OpenAliroDistanceSensor.async_added_to_hass`
- `OpenAliroDistanceSensor.update`
- `OpenAliroDistanceSensor.async_will_remove_from_hass`

</details>
