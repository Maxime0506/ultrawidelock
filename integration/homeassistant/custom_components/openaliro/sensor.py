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
        """Initialize the distance sensor entity with the runtime reference and a unique ID derived from the device ID."""
        self._runtime = runtime
        self._attr_unique_id = f"{runtime.device.device_id}_distance"
        self._remove_listener = None

    @property
    def available(self) -> bool:
        """Return whether the distance sensor is currently receiving valid range measurements from the lock."""
        return self._runtime.available

    @property
    def native_value(self) -> int | None:
        """Return the most recent measured distance to the lock in millimeters, or None if no measurement is available."""
        return self._runtime.distance_mm

    @property
    def device_info(self) -> DeviceInfo:
        """Return Home Assistant device metadata: a unique identifier derived from the lock's device ID and the openaliro manufacturer name."""
        return DeviceInfo(identifiers={(DOMAIN, self._runtime.device.device_id)}, manufacturer="openaliro")

    async def async_added_to_hass(self) -> None:
        """Register a listener callback that syncs the sensor state to Home Assistant when the runtime changes."""
        @callback
        def update() -> None:
            """Refresh the Home Assistant UI state to reflect the current distance value."""
            self.async_write_ha_state()

        self._remove_listener = self._runtime.add_listener(update)

    async def async_will_remove_from_hass(self) -> None:
        """Unregister the listener callback on entity removal."""
        if self._remove_listener:
            self._remove_listener()
