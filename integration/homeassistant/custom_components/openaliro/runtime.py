"""Home Assistant runtime bridge over the shared OpenAliro serial session."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Optional

from openaliro_ha import AccessEvent, DeviceConfig, DistanceReading, SerialSession, session_for_device


Listener = Callable[[], None]


class OpenAliroRuntime:
    """Own one direct serial session and fan out approved observations."""

    def __init__(
        self,
        device: DeviceConfig,
        session: Optional[SerialSession] = None,
        access_callback: Optional[Callable[[AccessEvent], None]] = None,
    ) -> None:
        """Initialize the OpenAliro runtime with a device configuration, optional serial session, and optional access callback. If no session is provided, create one via session_for_device. Set up internal state for distance, access events, listeners, and async tasks."""
        self.device = device
        self.session = session or session_for_device(device)
        self.distance_mm: Optional[int] = None
        self.last_access: Optional[AccessEvent] = None
        self._listeners: list[Listener] = []
        self._access_callback = access_callback
        self._stop = asyncio.Event()
        self._maintenance: Optional[asyncio.Task[None]] = None
        self._consumer: Optional[asyncio.Task[None]] = None

    @property
    def available(self) -> bool:
        """Return true if the device session is ready to stream or in compatibility mode."""
        return self.session.state.value in {"ready_streaming", "ready_compatibility"}

    async def async_start(self) -> None:
        """Start the runtime: spawn the maintenance coroutine to keep the serial session alive and the consumer coroutine to read observations indefinitely."""
        self._maintenance = asyncio.create_task(self.session.maintain(self._stop))
        self._consumer = asyncio.create_task(self._consume())

    async def async_stop(self) -> None:
        """Stop the runtime: set the stop flag, cancel and await the consumer and maintenance tasks (swallowing CancelledError), and close the session."""
        self._stop.set()
        for task in (self._consumer, self._maintenance):
            if task is not None:
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass
        await self.session.close()

    def add_listener(self, listener: Listener) -> Callable[[], None]:
        """Register a listener callback to be invoked whenever distance or access state changes. Return a callable that removes the listener."""
        self._listeners.append(listener)

        def remove() -> None:
            """Remove a listener from the callback list."""
            self._listeners.remove(listener)

        return remove

    async def _consume(self) -> None:
        """Coroutine that reads observations from the serial session indefinitely until _stop is set. For each observation: if DistanceReading, update distance_mm; if AccessEvent, update last_access and invoke the access callback if registered; otherwise skip. After each observation, notify all listeners."""
        while not self._stop.is_set():
            observation = await self.session.observations.get()
            if isinstance(observation, DistanceReading):
                self.distance_mm = observation.distance_mm
            elif isinstance(observation, AccessEvent):
                self.last_access = observation
                if self._access_callback is not None:
                    self._access_callback(observation)
            else:
                continue
            for listener in tuple(self._listeners):
                listener()
