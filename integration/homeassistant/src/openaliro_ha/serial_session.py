"""Async, transport-neutral ownership of one OpenAliro serial console.

The session is deliberately independent of pyserial and Home Assistant. A
runtime adapter provides an opened byte-stream; this module serializes shell
commands, parses only the approved observations, and never retains raw console
lines. The device can be idle after ``aliro frames on``: a stream acknowledgement
is therefore the capability probe, not the first range reading.
"""

from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from enum import Enum
from typing import Optional, Protocol, TypeVar

from .compatibility import RangeResponseParser
from .models import CompatibilityRangeReading, Observation
from .parser import parse_console_line, strip_ansi


class SerialConnection(Protocol):
    """The small async byte-stream contract needed by ``SerialSession``."""

    async def readline(self) -> bytes:
        """Return one newline-delimited serial line or ``b\"\"`` on disconnect."""

    async def write(self, data: bytes) -> None:
        """Queue bytes for delivery to the console."""

    def close(self) -> None:
        """Release the serial device."""


ConnectionFactory = Callable[[], Awaitable[SerialConnection]]


class SessionState(str, Enum):
    """Externally visible lifecycle states with no raw device details."""

    DISCONNECTED = "disconnected"
    DISCOVERING = "discovering"
    OPENING = "opening"
    PROBING = "probing"
    READY_STREAMING = "ready_streaming"
    READY_COMPATIBILITY = "ready_compatibility"
    BACKOFF = "backoff"
    CLOSED = "closed"


class SerialSessionError(RuntimeError):
    """A safe, user-facing serial session failure."""


class _ResponseHandler(Protocol):
    def feed(self, line: str) -> tuple[bool, object]:
        """Return whether a command response is complete and its safe result."""


Result = TypeVar("Result")


class _ContainsResponse:
    def __init__(self, expected: str, result: Result) -> None:
        self._expected = expected
        self._result = result

    def feed(self, line: str) -> tuple[bool, object]:
        return (self._expected in strip_ansi(line), self._result)


class _StreamResponse:
    """Accept the firmware's frames acknowledgement and retain its actual mode."""

    _ACKNOWLEDGEMENT = "per-block distance stream"

    def feed(self, line: str) -> tuple[bool, object]:
        text = strip_ansi(line)
        if self._ACKNOWLEDGEMENT not in text:
            return False, None
        return True, "on" in text


class _RangeResponse:
    def __init__(self) -> None:
        self._parser = RangeResponseParser()
        self._parser.begin()

    def feed(self, line: str) -> tuple[bool, object]:
        reading = self._parser.feed_line(line)
        if reading is not None:
            return True, reading
        if self._parser.finished:
            return True, None
        return False, None


class SerialSession:
    """Read console observations while issuing one shell command at a time."""

    def __init__(
        self,
        connection_factory: ConnectionFactory,
        *,
        command_timeout: float = 3.0,
        observation_queue_size: int = 256,
    ) -> None:
        if command_timeout <= 0:
            raise ValueError("command_timeout must be positive")
        if observation_queue_size <= 0:
            raise ValueError("observation_queue_size must be positive")
        self._connection_factory = connection_factory
        self._command_timeout = command_timeout
        self._connection: Optional[SerialConnection] = None
        self._reader_task: Optional[asyncio.Task[None]] = None
        self._response_handler: Optional[_ResponseHandler] = None
        self._response_future: Optional[asyncio.Future[object]] = None
        self._command_lock = asyncio.Lock()
        self._observations: asyncio.Queue[Observation] = asyncio.Queue(observation_queue_size)
        self._disconnected = asyncio.Event()
        self._state = SessionState.DISCONNECTED

    @property
    def state(self) -> SessionState:
        """Return the current lifecycle state."""

        return self._state

    @property
    def observations(self) -> asyncio.Queue[Observation]:
        """Expose parsed observations without exposing the raw console."""

        return self._observations

    async def start(self) -> SessionState:
        """Open, probe, and prepare the console without changing lock state."""

        if self._state not in {
            SessionState.DISCONNECTED,
            SessionState.CLOSED,
            SessionState.BACKOFF,
        }:
            raise SerialSessionError("serial session is already active")
        self._disconnected.clear()
        self._state = SessionState.DISCOVERING
        try:
            self._state = SessionState.OPENING
            self._connection = await self._connection_factory()
            self._reader_task = asyncio.create_task(self._read_loop())
            self._state = SessionState.PROBING
            await self._command("aliro version", _ContainsResponse("commit ", True))
            await self._command("aliro status", _ContainsResponse("chip", True))
            stream_enabled = await self._command("aliro frames on", _StreamResponse())
        except (OSError, asyncio.TimeoutError) as error:
            await self.close()
            raise SerialSessionError("serial console probe failed") from error
        except SerialSessionError:
            await self.close()
            raise
        self._state = (
            SessionState.READY_STREAMING if stream_enabled else SessionState.READY_COMPATIBILITY
        )
        return self._state

    async def poll_compatibility_range(self) -> Optional[CompatibilityRangeReading]:
        """Read one ``aliro range`` response while preserving unsolicited events."""

        if self._state not in {SessionState.READY_STREAMING, SessionState.READY_COMPATIBILITY}:
            raise SerialSessionError("serial session is not ready")
        result = await self._command("aliro range", _RangeResponse())
        if result is None:
            return None
        if not isinstance(result, CompatibilityRangeReading):
            raise SerialSessionError("invalid compatibility range response")
        return result

    async def maintain(self, stop_event: asyncio.Event, *, retry_delay: float = 1.0) -> None:
        """Reconnect until stopped, with a bounded caller-selected delay."""

        if retry_delay <= 0:
            raise ValueError("retry_delay must be positive")
        while not stop_event.is_set():
            try:
                await self.start()
                await self._wait_for_disconnect_or_stop(stop_event)
            except SerialSessionError:
                pass
            finally:
                await self.close()
            if stop_event.is_set():
                break
            self._state = SessionState.BACKOFF
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=retry_delay)
            except asyncio.TimeoutError:
                continue

    async def _wait_for_disconnect_or_stop(self, stop_event: asyncio.Event) -> None:
        """Wake promptly for either a transport loss or a caller-requested stop."""

        disconnected_wait = asyncio.create_task(self._disconnected.wait())
        stop_wait = asyncio.create_task(stop_event.wait())
        done, pending = await asyncio.wait(
            {disconnected_wait, stop_wait},
            return_when=asyncio.FIRST_COMPLETED,
        )
        del done
        for task in pending:
            task.cancel()
        if pending:
            await asyncio.gather(*pending, return_exceptions=True)

    async def close(self) -> None:
        """Stop I/O, fail any pending command, and close the owned transport."""

        connection, self._connection = self._connection, None
        reader_task, self._reader_task = self._reader_task, None
        self._fail_pending(SerialSessionError("serial console disconnected"))
        if reader_task and reader_task is not asyncio.current_task():
            reader_task.cancel()
            try:
                await reader_task
            except asyncio.CancelledError:
                pass
        if connection:
            connection.close()
        self._disconnected.set()
        self._state = SessionState.CLOSED

    async def _command(self, command: str, handler: _ResponseHandler) -> object:
        async with self._command_lock:
            connection = self._connection
            if connection is None:
                raise SerialSessionError("serial console is disconnected")
            loop = asyncio.get_running_loop()
            future: asyncio.Future[object] = loop.create_future()
            self._response_handler = handler
            self._response_future = future
            try:
                await connection.write(f"{command}\n".encode("ascii"))
                return await asyncio.wait_for(future, timeout=self._command_timeout)
            except asyncio.TimeoutError as error:
                raise SerialSessionError("serial command timed out") from error
            finally:
                if self._response_future is future:
                    self._response_handler = None
                    self._response_future = None

    async def _read_loop(self) -> None:
        try:
            while self._connection is not None:
                data = await self._connection.readline()
                if not data:
                    raise OSError("serial console closed")
                line = data.decode("utf-8", errors="replace")
                observation = parse_console_line(line)
                if observation is not None:
                    try:
                        self._observations.put_nowait(observation)
                    except asyncio.QueueFull as error:
                        raise SerialSessionError("serial observation queue overflow") from error
                self._feed_response(line)
        except asyncio.CancelledError:
            raise
        except (OSError, SerialSessionError):
            self._fail_pending(SerialSessionError("serial console disconnected"))
            self._disconnected.set()
            self._state = SessionState.DISCONNECTED

    def _feed_response(self, line: str) -> None:
        handler, future = self._response_handler, self._response_future
        if handler is None or future is None or future.done():
            return
        complete, result = handler.feed(line)
        if complete:
            future.set_result(result)

    def _fail_pending(self, error: SerialSessionError) -> None:
        future = self._response_future
        if future is not None and not future.done():
            future.set_exception(error)
