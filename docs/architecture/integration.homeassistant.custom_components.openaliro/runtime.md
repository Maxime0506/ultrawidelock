<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/runtime.py`

Home Assistant runtime bridge over the shared OpenAliro serial session.

**used by** [`integration/homeassistant/custom_components/openaliro/__init__.py`](__init__.md), [`integration/homeassistant/custom_components/openaliro/diagnostics.py`](diagnostics.md), [`integration/homeassistant/custom_components/openaliro/event.py`](event.md), [`integration/homeassistant/custom_components/openaliro/sensor.py`](sensor.md)

## API

### `class OpenAliroRuntime`
`integration/homeassistant/custom_components/openaliro/runtime.py:15`

Own one direct serial session and fan out approved observations.

#### `OpenAliroRuntime.__init__(self, device: DeviceConfig, session: Optional[SerialSession]=None, access_callback: Optional[Callable[[AccessEvent], None]]=None) -> None`
`integration/homeassistant/custom_components/openaliro/runtime.py:18`

Initialize the OpenAliro runtime with a device configuration, optional serial session, and optional access callback. If no session is provided, create one via session_for_device. Set up internal state for distance, access events, listeners, and async tasks.

#### `OpenAliroRuntime.available(self) -> bool`
`integration/homeassistant/custom_components/openaliro/runtime.py:36`

Return true if the device session is ready to stream or in compatibility mode.

#### `OpenAliroRuntime.async_start(self) -> None`
`integration/homeassistant/custom_components/openaliro/runtime.py:40`

Start the runtime: spawn the maintenance coroutine to keep the serial session alive and the consumer coroutine to read observations indefinitely.

**calls** `OpenAliroRuntime._consume`

#### `OpenAliroRuntime.async_stop(self) -> None`
`integration/homeassistant/custom_components/openaliro/runtime.py:45`

Stop the runtime: set the stop flag, cancel and await the consumer and maintenance tasks (swallowing CancelledError), and close the session.

#### `OpenAliroRuntime.add_listener(self, listener: Listener) -> Callable[[], None]`
`integration/homeassistant/custom_components/openaliro/runtime.py:57`

Register a listener callback to be invoked whenever distance or access state changes. Return a callable that removes the listener.

#### `OpenAliroRuntime._consume(self) -> None`
`integration/homeassistant/custom_components/openaliro/runtime.py:67`

Coroutine that reads observations from the serial session indefinitely until _stop is set. For each observation: if DistanceReading, update distance_mm; if AccessEvent, update last_access and invoke the access callback if registered; otherwise skip. After each observation, notify all listeners.

**called by** `OpenAliroRuntime.async_start`

<details><summary>Undocumented (1)</summary>

- `OpenAliroRuntime.remove` — tested: a removed listener stops being called

</details>
