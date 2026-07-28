"""Access outcome event entity for the HA=1 OpenAliro direct integration."""

from homeassistant.components.event import EventEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .runtime import OpenAliroRuntime


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry[OpenAliroRuntime], async_add_entities: AddEntitiesCallback
) -> None:
    """Create the credential-free access event entity."""

    async_add_entities([OpenAliroAccessEvent(entry.runtime_data)])


class OpenAliroAccessEvent(EventEntity):
    """Emit exactly the granted and denied outcomes parsed by the shared library."""

    _attr_event_types = ["granted", "denied"]
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_translation_key = "access"

    def __init__(self, runtime: OpenAliroRuntime) -> None:
        self._runtime = runtime
        self._attr_unique_id = f"{runtime.device.device_id}_access"
        self._remove_listener = None

    @property
    def available(self) -> bool:
        return self._runtime.available

    @property
    def device_info(self) -> DeviceInfo:
        return DeviceInfo(identifiers={(DOMAIN, self._runtime.device.device_id)}, manufacturer="openaliro")

    async def async_added_to_hass(self) -> None:
        @callback
        def update() -> None:
            event = self._runtime.last_access
            if event is not None:
                self._trigger_event(event.verdict)
            self.async_write_ha_state()

        self._remove_listener = self._runtime.add_listener(update)

    async def async_will_remove_from_hass(self) -> None:
        if self._remove_listener:
            self._remove_listener()
