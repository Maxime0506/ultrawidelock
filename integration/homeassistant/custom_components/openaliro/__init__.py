"""HA=1-only OpenAliro direct-serial integration."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant

from openaliro_ha import DeviceConfig

from .const import CONF_BAUD, CONF_DEVICE_ID, CONF_SERIAL_IDENTITY, CONF_SERIAL_PORT, DOMAIN, PLATFORMS
from .runtime import OpenAliroRuntime


type OpenAliroConfigEntry = ConfigEntry[OpenAliroRuntime]


async def async_setup_entry(hass: HomeAssistant, entry: OpenAliroConfigEntry) -> bool:
    """Set up direct serial ownership for one config entry."""

    runtime = OpenAliroRuntime(
        DeviceConfig(
            device_id=entry.data[CONF_DEVICE_ID],
            serial_port=entry.data[CONF_SERIAL_PORT],
            serial_identity=entry.data[CONF_SERIAL_IDENTITY],
            baud=entry.data[CONF_BAUD],
        )
    )
    entry.runtime_data = runtime
    await runtime.async_start()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: OpenAliroConfigEntry) -> bool:
    """Unload platforms and release the exclusive serial port."""

    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await entry.runtime_data.async_stop()
    return unloaded
