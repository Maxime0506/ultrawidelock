<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/compatibility.py`

Incremental parser for the source-proven ``aliro range`` compatibility mode.

**depends on** [`integration/homeassistant/src/openaliro_ha/models.py`](models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](serial_session.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class RangeResponseParser`
`integration/homeassistant/src/openaliro_ha/compatibility.py:18`

Parse one explicitly started ``aliro range`` command response at a time.

#### `RangeResponseParser.__init__(self) -> None`
`integration/homeassistant/src/openaliro_ha/compatibility.py:21`

Initialize a range response parser. Sets all internal state to inactive with no measurements.

#### `RangeResponseParser.begin(self) -> None`
`integration/homeassistant/src/openaliro_ha/compatibility.py:30`

Start a new response, discarding any interrupted prior response.

#### `RangeResponseParser.feed_line(self, line: str) -> Optional[CompatibilityRangeReading]`
`integration/homeassistant/src/openaliro_ha/compatibility.py:40`

Accept one response line and emit only a complete, internally valid reading.

**calls** `RangeResponseParser._complete_reading`

#### `RangeResponseParser.finished(self) -> bool`
`integration/homeassistant/src/openaliro_ha/compatibility.py:70`

Whether the active response reached a safe terminal line.

#### `RangeResponseParser._complete_reading(self, trusted: bool) -> Optional[CompatibilityRangeReading]`
`integration/homeassistant/src/openaliro_ha/compatibility.py:75`

Emit a complete reading if all four measurements (distance, NLOS, block, age) are present and non-negative; return None otherwise.

**called by** `RangeResponseParser.feed_line`
