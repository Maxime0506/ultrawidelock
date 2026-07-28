"""Constants for the HA=1 OpenAliro custom integration."""

from homeassistant.const import Platform


DOMAIN = "openaliro"
PLATFORMS = [Platform.SENSOR, Platform.EVENT]
CONF_BAUD = "baud"
CONF_DEVICE_ID = "device_id"
CONF_SERIAL_IDENTITY = "serial_identity"
CONF_SERIAL_PORT = "serial_port"
EVENT_ACCESS = "openaliro_access"
EVENT_DEVICE_ID = "openaliro_device_id"
