<!-- generated documentation — edit the source, not this file -->
# `integration/homeassistant/custom_components/openaliro/device_trigger.py`

Granted and denied device-automation triggers for OpenAliro access events.

**depends on** [`integration/homeassistant/custom_components/openaliro/const.py`](const.md)

```mermaid
flowchart TD
  async_attach_trigger --> _openaliro_identifier
```

## API

### `async_get_triggers(hass: HomeAssistant, device_id: str) -> list[dict[str, str]]`
`integration/homeassistant/custom_components/openaliro/device_trigger.py:30`

List the two credential-free access outcome triggers.

**calls** `_openaliro_identifier`

### `async_attach_trigger(hass: HomeAssistant, config: ConfigType, action: TriggerActionType, trigger_info: TriggerInfo) -> CALLBACK_TYPE`
`integration/homeassistant/custom_components/openaliro/device_trigger.py:40`

Attach an automation to one access outcome without exposing credential data.

**calls** `_openaliro_identifier`

<details><summary>Undocumented (1)</summary>

- `_openaliro_identifier`

</details>
