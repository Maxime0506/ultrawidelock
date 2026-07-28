"""Redacted diagnostics for the HA=1 OpenAliro direct integration."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant

from .const import CONF_BAUD, CONF_DEVICE_ID, CONF_SERIAL_IDENTITY
from .runtime import OpenAliroRuntime


async def async_get_config_entry_diagnostics(
    hass: HomeAssistant, entry: ConfigEntry[OpenAliroRuntime]
) -> dict[str, object]:
    """Return support data without serial paths, USB serials, or console text."""

    del hass
    runtime = entry.runtime_data
    return {
        "config": {
            "device_id": entry.data[CONF_DEVICE_ID],
            "baud": entry.data[CONF_BAUD],
            "serial_identity_configured": bool(entry.data[CONF_SERIAL_IDENTITY]),
        },
        "runtime": {
            "session_state": runtime.session.state.value,
            "distance_available": runtime.distance_mm is not None,
            "last_access": runtime.last_access.verdict if runtime.last_access else None,
        },
    }
