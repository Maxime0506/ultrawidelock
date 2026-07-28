"""Typed observations emitted by the HA=1 console parser."""

from dataclasses import dataclass
from typing import Literal, Optional, Union


@dataclass(frozen=True)
class DistanceReading:
    """One UWB distance reading; ``block`` is absent on the raw ``DIST`` line."""

    block: Optional[int]
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
