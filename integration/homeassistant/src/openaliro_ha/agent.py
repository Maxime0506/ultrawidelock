"""Runnable standalone-agent orchestration over the shared serial library."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from dataclasses import dataclass
from typing import Optional

from .config import AgentConfig, DeviceConfig
from .models import AccessEvent, DistanceReading
from .mqtt import MqttPublisher
from .serial_session import SerialSession, SessionState
from .serial_transport import open_serial_connection, resolve_serial_port


class AgentError(RuntimeError):
    """A safe agent failure with no raw serial, broker, or console details."""


@dataclass(frozen=True)
class DoctorDeviceResult:
    """One diagnostics-safe device result."""

    device_id: str
    serial_state: str


PublisherFactory = Callable[[AgentConfig, DeviceConfig], MqttPublisher]


def _publisher(config: AgentConfig, device: DeviceConfig) -> MqttPublisher:
    return MqttPublisher(config.mqtt, device.device_id)


def session_for_device(device: DeviceConfig) -> SerialSession:
    """Build a reconnecting serial session that resolves ``auto`` on every open."""

    async def connection_factory():
        port = resolve_serial_port(device.serial_port, device.serial_identity)
        return await open_serial_connection(port, device.baud)

    return SerialSession(connection_factory)


async def doctor(
    config: AgentConfig,
    *,
    publisher_factory: PublisherFactory = _publisher,
    session_factory: Callable[[DeviceConfig], SerialSession] = session_for_device,
) -> tuple[DoctorDeviceResult, ...]:
    """Validate serial protocol and broker connectivity without publishing events."""

    results = []
    for device in config.devices:
        session = session_factory(device)
        try:
            state = await session.start()
        except Exception as error:
            raise AgentError("serial console compatibility check failed") from error
        finally:
            await session.close()
        if state not in {SessionState.READY_STREAMING, SessionState.READY_COMPATIBILITY}:
            raise AgentError("serial console did not reach a ready state")
        results.append(DoctorDeviceResult(device.device_id, state.value))

    for device in config.devices:
        publisher = publisher_factory(config, device)
        try:
            await asyncio.to_thread(publisher.start)
            await asyncio.to_thread(publisher.close)
        except Exception as error:
            raise AgentError("MQTT authentication, TLS, or broker check failed") from error
    return tuple(results)


async def probe_device(device: DeviceConfig) -> SessionState:
    """Probe one selected port before configuration persists any settings."""

    session = session_for_device(device)
    try:
        return await session.start()
    except Exception as error:
        raise AgentError("selected serial interface is not a compatible OpenAliro console") from error
    finally:
        await session.close()


async def run_device(
    config: AgentConfig,
    device: DeviceConfig,
    stop_event: asyncio.Event,
    *,
    publisher_factory: PublisherFactory = _publisher,
    session_factory: Callable[[DeviceConfig], SerialSession] = session_for_device,
) -> None:
    """Publish one device's approved observations until a caller stops the agent."""

    publisher = publisher_factory(config, device)
    session = session_factory(device)
    try:
        await asyncio.to_thread(publisher.start)
        maintenance = asyncio.create_task(session.maintain(stop_event))
        try:
            while not stop_event.is_set():
                observation_task = asyncio.create_task(session.observations.get())
                stop_task = asyncio.create_task(stop_event.wait())
                done, pending = await asyncio.wait(
                    {observation_task, stop_task},
                    return_when=asyncio.FIRST_COMPLETED,
                )
                for task in pending:
                    task.cancel()
                if pending:
                    await asyncio.gather(*pending, return_exceptions=True)
                if stop_task in done:
                    break
                observation = observation_task.result()
                if isinstance(observation, DistanceReading):
                    await asyncio.to_thread(publisher.publish_distance, observation)
                elif isinstance(observation, AccessEvent):
                    await asyncio.to_thread(publisher.publish_access, observation)
        finally:
            stop_event.set()
            await maintenance
    except Exception as error:
        if isinstance(error, AgentError):
            raise
        raise AgentError("standalone agent stopped because a transport failed") from error
    finally:
        await session.close()
        await asyncio.to_thread(publisher.close)


async def run(config: AgentConfig, stop_event: Optional[asyncio.Event] = None) -> None:
    """Run every configured lock concurrently until the supplied event is set."""

    event = stop_event or asyncio.Event()
    await asyncio.gather(*(run_device(config, device, event) for device in config.devices))
