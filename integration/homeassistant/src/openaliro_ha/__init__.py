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
from .models import AccessEvent, CompatibilityRangeReading, DistanceReading, Observation
from .mqtt import MqttError, MqttPublisher
from .parser import parse_console_line, strip_ansi

__all__ = (
    "AgentConfig",
    "AccessEvent",
    "CompatibilityRangeReading",
    "ConfigError",
    "DeviceConfig",
    "DistanceReading",
    "MqttConfig",
    "MqttError",
    "MqttPublisher",
    "Observation",
    "RangeResponseParser",
    "load_config",
    "parse_console_line",
    "redacted_config",
    "strip_ansi",
    "write_config",
)
