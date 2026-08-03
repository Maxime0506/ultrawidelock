<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/scripts/ha-setup.sh`

One command from nothing to a working OpenAliro Home Assistant agent.
Generates the broker TLS material, installs it into the Home Assistant
Mosquitto add-on over SSH, writes the agent configuration, and runs doctor.
Every step is idempotent: re-running repairs whatever drifted.
Override any default with an environment variable, for example
HA_SSH=my-hass BROKER_HOST=hass.lan ./ha-setup.sh

## API

### `step()`
`integration/homeassistant/scripts/ha-setup.sh:28`

Print a progress step message showing the current step number out of the total.

### `fail()`
`integration/homeassistant/scripts/ha-setup.sh:30`

Print an error message to stderr prefixed with ha-setup and exit with status 1.

### `write_config()`
`integration/homeassistant/scripts/ha-setup.sh:147`

configure merges: the MQTT block comes from these flags and any device other
than this one is carried over, so an existing multi-device config survives.
A config too damaged to parse would stop it, so that one is moved aside and
named, never silently discarded.
