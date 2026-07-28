<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/models.py`

Typed observations emitted by the HA=1 console parser.

**used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/agent.py`](agent.md), [`integration/homeassistant/src/openaliro_ha/cli.py`](cli.md), [`integration/homeassistant/src/openaliro_ha/compatibility.py`](compatibility.md), [`integration/homeassistant/src/openaliro_ha/mqtt.py`](mqtt.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](parser.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](serial_session.md)

## API

### `class DistanceReading`
`integration/homeassistant/src/openaliro_ha/models.py:8`

One UWB distance reading; ``block`` is absent on the raw ``DIST`` line.

### `class AccessEvent`
`integration/homeassistant/src/openaliro_ha/models.py:17`

A credential-independent access outcome.

### `class CompatibilityRangeReading`
`integration/homeassistant/src/openaliro_ha/models.py:24`

A lower-resolution reading assembled from an ``aliro range`` response.

The peer address printed by firmware is intentionally not represented.
