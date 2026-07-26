"""Typed observations emitted by the HA=1 console parser."""

from dataclasses import dataclass
from typing import Literal, Union


@dataclass(frozen=True)
class DistanceReading:
    """One per-block UWB distance reading from the curated console stream."""

    block: int
    distance_mm: int
    tof: int


@dataclass(frozen=True)
class AccessEvent:
    """A credential-independent access outcome."""

    verdict: Literal["granted", "denied"]


@dataclass(frozen=True)
class CompatibilityRangeReading:
    """A lower-resolution reading assembled from an ``aliro range`` response.

    The peer address printed by firmware is intentionally not represented.
    """

    block: int
    distance_mm: int
    age_ms: int
    trusted: bool
    nlos: bool


Observation = Union[DistanceReading, AccessEvent, CompatibilityRangeReading]
