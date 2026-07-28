"""HA=1-only staging library for the OpenAliro Home Assistant adapters.

This is intentionally not a published distribution or a stable public API yet.
The direct Home Assistant adapter remains blocked on Stage 0 hardware evidence.
"""

__version__ = "0.0.0"

from .config import (
    AgentConfig,
    ConfigError,
    DeviceConfig,
    MqttConfig,
    load_config,
    redacted_config,
    write_config,
)
from .compatibility import RangeResponseParser
from .agent import AgentError, DoctorDeviceResult, doctor, probe_device, run, run_device, session_for_device
from .models import AccessEvent, CompatibilityRangeReading, DistanceReading, Observation
from .mqtt import MqttError, MqttPublisher
from .parser import parse_console_line, strip_ansi
from .serial_session import SerialConnection, SerialSession, SerialSessionError, SessionState
from .serial_transport import (
    PySerialConnection,
    SerialPort,
    SerialTransportError,
    discover_serial_ports,
    open_serial_connection,
    resolve_serial_port,
    serial_identity,
)

__all__ = (
    "AgentConfig",
    "AgentError",
    "AccessEvent",
    "CompatibilityRangeReading",
    "ConfigError",
    "DeviceConfig",
    "DistanceReading",
    "DoctorDeviceResult",
    "MqttConfig",
    "MqttError",
    "MqttPublisher",
    "Observation",
    "RangeResponseParser",
    "SerialConnection",
    "SerialSession",
    "SerialSessionError",
    "SerialPort",
    "SerialTransportError",
    "PySerialConnection",
    "SessionState",
    "load_config",
    "doctor",
    "parse_console_line",
    "probe_device",
    "redacted_config",
    "strip_ansi",
    "discover_serial_ports",
    "open_serial_connection",
    "resolve_serial_port",
    "run",
    "run_device",
    "session_for_device",
    "serial_identity",
    "write_config",
)
