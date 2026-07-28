"""Granted and denied device-automation triggers for OpenAliro access events."""

import voluptuous as vol

from homeassistant.components.device_automation import DEVICE_TRIGGER_BASE_SCHEMA, DeviceNotFound
from homeassistant.components.homeassistant.triggers import event as event_trigger
from homeassistant.const import CONF_DEVICE_ID, CONF_DOMAIN, CONF_PLATFORM, CONF_TYPE
from homeassistant.core import CALLBACK_TYPE, HomeAssistant
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.trigger import TriggerActionType, TriggerInfo
from homeassistant.helpers.typing import ConfigType

from .const import DOMAIN, EVENT_ACCESS, EVENT_DEVICE_ID


TRIGGER_TYPES = {"access_granted": "granted", "access_denied": "denied"}
TRIGGER_SCHEMA = DEVICE_TRIGGER_BASE_SCHEMA.extend({vol.Required(CONF_TYPE): vol.In(TRIGGER_TYPES)})


def _openaliro_identifier(hass: HomeAssistant, device_id: str) -> str:
    device = dr.async_get(hass).async_get(device_id)
    if device is None:
        raise DeviceNotFound(f"{device_id} is not valid")
    for domain, identifier in device.identifiers:
        if domain == DOMAIN:
            return identifier
    raise DeviceNotFound(f"{device_id} is not an OpenAliro device")


async def async_get_triggers(hass: HomeAssistant, device_id: str) -> list[dict[str, str]]:
    """List the two credential-free access outcome triggers."""

    _openaliro_identifier(hass, device_id)
    return [
        {CONF_PLATFORM: "device", CONF_DOMAIN: DOMAIN, CONF_DEVICE_ID: device_id, CONF_TYPE: trigger}
        for trigger in TRIGGER_TYPES
    ]


async def async_attach_trigger(
    hass: HomeAssistant, config: ConfigType, action: TriggerActionType, trigger_info: TriggerInfo
) -> CALLBACK_TYPE:
    """Attach an automation to one access outcome without exposing credential data."""

    device_identifier = _openaliro_identifier(hass, config[CONF_DEVICE_ID])
    event_config = event_trigger.TRIGGER_SCHEMA(
        {
            event_trigger.CONF_PLATFORM: "event",
            event_trigger.CONF_EVENT_TYPE: EVENT_ACCESS,
            event_trigger.CONF_EVENT_DATA: {
                EVENT_DEVICE_ID: device_identifier,
                "type": TRIGGER_TYPES[config[CONF_TYPE]],
            },
        }
    )
    return await event_trigger.async_attach_trigger(
        hass, event_config, action, trigger_info, platform_type="device"
    )
