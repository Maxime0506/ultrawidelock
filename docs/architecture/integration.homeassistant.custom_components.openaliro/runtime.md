<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/runtime.py`

Home Assistant runtime bridge over the shared OpenAliro serial session.

**used by** [`integration/homeassistant/custom_components/openaliro/__init__.py`](__init__.md), [`integration/homeassistant/custom_components/openaliro/diagnostics.py`](diagnostics.md), [`integration/homeassistant/custom_components/openaliro/event.py`](event.md), [`integration/homeassistant/custom_components/openaliro/sensor.py`](sensor.md)

## API

### `class OpenAliroRuntime`
`integration/homeassistant/custom_components/openaliro/runtime.py:15`

Own one direct serial session and fan out approved observations.

<details><summary>Undocumented (7)</summary>

- `OpenAliroRuntime.__init__`
- `OpenAliroRuntime.available`
- `OpenAliroRuntime.async_start` — tested: a removed listener stops being called; access event is recorded and forwarded; distance reaches entities and notifies listeners; stop closes the session and cancels its tasks; stop is safe before any observation arrives
- `OpenAliroRuntime.async_stop` — tested: a removed listener stops being called; access event is recorded and forwarded; distance reaches entities and notifies listeners; stop closes the session and cancels its tasks; stop is safe before any observation arrives
- `OpenAliroRuntime.add_listener` — tested: a removed listener stops being called; distance reaches entities and notifies listeners
- `OpenAliroRuntime.remove` — tested: a removed listener stops being called
- `OpenAliroRuntime._consume`

</details>
