<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/scripts/ha-setup.sh`

One command from nothing to a working OpenAliro Home Assistant agent.
Generates the broker TLS material, installs it into the Home Assistant
Mosquitto add-on over SSH, writes the agent configuration, and runs doctor.
Every step is idempotent: re-running repairs whatever drifted.
Override any default with an environment variable, for example
HA_SSH=my-hass BROKER_HOST=hass.lan ./ha-setup.sh

<details><summary>Undocumented (2)</summary>

- `step`
- `fail`

</details>
