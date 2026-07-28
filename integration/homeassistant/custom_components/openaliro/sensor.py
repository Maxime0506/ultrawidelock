"""Distance sensor for the HA=1 OpenAliro direct integration."""

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfLength
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .runtime import OpenAliroRuntime


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry[OpenAliroRuntime], async_add_entities: AddEntitiesCallback
) -> None:
    """Create the one supported distance entity."""

    async_add_entities([OpenAliroDistanceSensor(entry.runtime_data)])


class OpenAliroDistanceSensor(SensorEntity):
    """Latest valid UWB range, without peer or credential metadata."""

    _attr_device_class = SensorDeviceClass.DISTANCE
    _attr_native_unit_of_measurement = UnitOfLength.MILLIMETERS
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_translation_key = "distance"

    def __init__(self, runtime: OpenAliroRuntime) -> None:
        self._runtime = runtime
        self._attr_unique_id = f"{runtime.device.device_id}_distance"
        self._remove_listener = None

    @property
    def available(self) -> bool:
        return self._runtime.available

    @property
    def native_value(self) -> int | None:
        return self._runtime.distance_mm

    @property
    def device_info(self) -> DeviceInfo:
        return DeviceInfo(identifiers={(DOMAIN, self._runtime.device.device_id)}, manufacturer="openaliro")

    async def async_added_to_hass(self) -> None:
        @callback
        def update() -> None:
            self.async_write_ha_state()

        self._remove_listener = self._runtime.add_listener(update)

    async def async_will_remove_from_hass(self) -> None:
        if self._remove_listener:
            self._remove_listener()
