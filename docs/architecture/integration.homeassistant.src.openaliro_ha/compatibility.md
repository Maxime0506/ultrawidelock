<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/src/openaliro_ha/compatibility.py`

Incremental parser for the source-proven ``aliro range`` compatibility mode.

**depends on** [`integration/homeassistant/src/openaliro_ha/models.py`](models.md), [`integration/homeassistant/src/openaliro_ha/parser.py`](parser.md)  ·  **used by** [`integration/homeassistant/src/openaliro_ha/__init__.py`](__init__.md), [`integration/homeassistant/src/openaliro_ha/serial_session.py`](serial_session.md)  ·  **discussed in** [`docs/home-assistant-internals.md`](../../home-assistant-internals.md)

## API

### `class RangeResponseParser`
`integration/homeassistant/src/openaliro_ha/compatibility.py:18`

Parse one explicitly started ``aliro range`` command response at a time.

#### `RangeResponseParser.begin(self) -> None`
`integration/homeassistant/src/openaliro_ha/compatibility.py:29`

Start a new response, discarding any interrupted prior response.

#### `RangeResponseParser.feed_line(self, line: str) -> Optional[CompatibilityRangeReading]`
`integration/homeassistant/src/openaliro_ha/compatibility.py:39`

Accept one response line and emit only a complete, internally valid reading.

**calls** `RangeResponseParser._complete_reading`

#### `RangeResponseParser.finished(self) -> bool`
`integration/homeassistant/src/openaliro_ha/compatibility.py:69`

Whether the active response reached a safe terminal line.

<details><summary>Undocumented (2)</summary>

- `RangeResponseParser.__init__`
- `RangeResponseParser._complete_reading`

</details>
