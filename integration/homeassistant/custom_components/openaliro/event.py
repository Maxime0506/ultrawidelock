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
        """Initialize the access event entity with the runtime reference and a unique ID derived from the device ID."""
        self._runtime = runtime
        self._attr_unique_id = f"{runtime.device.device_id}_access"
        self._remove_listener = None

    @property
    def available(self) -> bool:
        """Return the runtime availability (true if the connection is up)."""
        return self._runtime.available

    @property
    def device_info(self) -> DeviceInfo:
        """Return device metadata linking this entity to the OpenAliro device."""
        return DeviceInfo(identifiers={(DOMAIN, self._runtime.device.device_id)}, manufacturer="openaliro")

    async def async_added_to_hass(self) -> None:
        """Register a listener callback that publishes the last access verdict as a Home Assistant event when the runtime changes state."""
        @callback
        def update() -> None:
            """Invoke when the runtime updates: trigger an event on a fresh access verdict and write the state to Home Assistant."""
            event = self._runtime.last_access
            if event is not None:
                self._trigger_event(event.verdict)
            self.async_write_ha_state()

        self._remove_listener = self._runtime.add_listener(update)

    async def async_will_remove_from_hass(self) -> None:
        """Unregister the listener callback on entity removal."""
        if self._remove_listener:
            self._remove_listener()
