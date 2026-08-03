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

#### `OpenAliroDistanceSensor.__init__(self, runtime: OpenAliroRuntime) -> None`
`integration/homeassistant/custom_components/openaliro/sensor.py:31`

Initialize the distance sensor entity with the runtime reference and a unique ID derived from the device ID.

#### `OpenAliroDistanceSensor.available(self) -> bool`
`integration/homeassistant/custom_components/openaliro/sensor.py:38`

Return whether the distance sensor is currently receiving valid range measurements from the lock.

#### `OpenAliroDistanceSensor.native_value(self) -> int | None`
`integration/homeassistant/custom_components/openaliro/sensor.py:43`

Return the most recent measured distance to the lock in millimeters, or None if no measurement is available.

#### `OpenAliroDistanceSensor.device_info(self) -> DeviceInfo`
`integration/homeassistant/custom_components/openaliro/sensor.py:48`

Return Home Assistant device metadata: a unique identifier derived from the lock's device ID and the openaliro manufacturer name.

#### `OpenAliroDistanceSensor.async_added_to_hass(self) -> None`
`integration/homeassistant/custom_components/openaliro/sensor.py:52`

Register a listener callback that syncs the sensor state to Home Assistant when the runtime changes.

#### `OpenAliroDistanceSensor.async_will_remove_from_hass(self) -> None`
`integration/homeassistant/custom_components/openaliro/sensor.py:61`

Unregister the listener callback on entity removal.

<details><summary>Undocumented (1)</summary>

- `OpenAliroDistanceSensor.update`

</details>
