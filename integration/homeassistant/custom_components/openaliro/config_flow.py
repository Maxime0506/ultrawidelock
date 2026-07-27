"""Manual direct-serial config flow for the HA=1 OpenAliro beta."""

from __future__ import annotations

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResult

from openaliro_ha import DeviceConfig, SerialTransportError, discover_serial_ports, probe_device

from .const import CONF_BAUD, CONF_DEVICE_ID, CONF_SERIAL_IDENTITY, CONF_SERIAL_PORT, DOMAIN


class OpenAliroConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Ask for a serial port, verify it, and retain a hashed USB identity."""

    VERSION = 1

    async def async_step_user(self, user_input: dict[str, object] | None = None) -> FlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            serial_port = str(user_input[CONF_SERIAL_PORT])
            selected = next((port for port in discover_serial_ports() if port.device == serial_port), None)
            if selected is None or selected.identity is None:
                errors["base"] = "serial_identity_unavailable"
            else:
                device_id = f"openaliro-{selected.identity[:8]}"
                device = DeviceConfig(
                    device_id=device_id,
                    serial_port=serial_port,
                    serial_identity=selected.identity,
                    baud=int(user_input[CONF_BAUD]),
                )
                try:
                    await probe_device(device)
                except (SerialTransportError, RuntimeError):
                    errors["base"] = "cannot_connect"
                else:
                    await self.async_set_unique_id(selected.identity)
                    self._abort_if_unique_id_configured()
                    return self.async_create_entry(
                        title="OpenAliro",
                        data={
                            CONF_DEVICE_ID: device_id,
                            CONF_SERIAL_PORT: serial_port,
                            CONF_SERIAL_IDENTITY: selected.identity,
                            CONF_BAUD: device.baud,
                        },
                    )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_SERIAL_PORT): str,
                    vol.Required(CONF_BAUD, default=115200): int,
                }
            ),
            errors=errors,
        )

    async def async_step_reconfigure(self, user_input: dict[str, object] | None = None) -> FlowResult:
        """Replace only the selected interface after a USB port change."""

        entry = self._get_reconfigure_entry()
        if user_input is not None:
            serial_port = str(user_input[CONF_SERIAL_PORT])
            selected = next((port for port in discover_serial_ports() if port.device == serial_port), None)
            if selected is not None and selected.identity == entry.data[CONF_SERIAL_IDENTITY]:
                updates = {CONF_SERIAL_PORT: serial_port, CONF_BAUD: int(user_input[CONF_BAUD])}
                return self.async_update_reload_and_abort(entry, data_updates=updates)
            return self.async_show_form(step_id="reconfigure", errors={"base": "serial_identity_unavailable"})
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_SERIAL_PORT, default=entry.data[CONF_SERIAL_PORT]): str,
                    vol.Required(CONF_BAUD, default=entry.data[CONF_BAUD]): int,
                }
            ),
        )
