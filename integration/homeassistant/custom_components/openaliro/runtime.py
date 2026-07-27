"""Home Assistant runtime bridge over the shared OpenAliro serial session."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Optional

from openaliro_ha import AccessEvent, DeviceConfig, DistanceReading, SerialSession, session_for_device


Listener = Callable[[], None]


class OpenAliroRuntime:
    """Own one direct serial session and fan out approved observations."""

    def __init__(self, device: DeviceConfig, session: Optional[SerialSession] = None) -> None:
        self.device = device
        self.session = session or session_for_device(device)
        self.distance_mm: Optional[int] = None
        self.last_access: Optional[AccessEvent] = None
        self._listeners: list[Listener] = []
        self._stop = asyncio.Event()
        self._maintenance: Optional[asyncio.Task[None]] = None
        self._consumer: Optional[asyncio.Task[None]] = None

    @property
    def available(self) -> bool:
        return self.session.state.value in {"ready_streaming", "ready_compatibility"}

    async def async_start(self) -> None:
        self._maintenance = asyncio.create_task(self.session.maintain(self._stop))
        self._consumer = asyncio.create_task(self._consume())

    async def async_stop(self) -> None:
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
        self._listeners.append(listener)

        def remove() -> None:
            self._listeners.remove(listener)

        return remove

    async def _consume(self) -> None:
        while not self._stop.is_set():
            observation = await self.session.observations.get()
            if isinstance(observation, DistanceReading):
                self.distance_mm = observation.distance_mm
            elif isinstance(observation, AccessEvent):
                self.last_access = observation
            else:
                continue
            for listener in tuple(self._listeners):
                listener()
