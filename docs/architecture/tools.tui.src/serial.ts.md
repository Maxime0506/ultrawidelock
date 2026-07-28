<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/serial.ts`

**used by** [`integration/homeassistant/aliro_mqtt_bridge.py`](../integration.homeassistant/aliro_mqtt_bridge.md), [`tools/tui/src/app.tsx`](app.tsx.md), [`tools/tui/src/targets.ts`](targets.ts.md)

<details><summary>Undocumented (16)</summary>

- `kindForVendor`
- `parseMacSerialInventory` — tested: :classifies mac os serial devices by usb vendor for compatibility checks@l4; :keeps one usb device's vendor across all of its serial interfaces@l33; :never lets one device's vendor identify the next device's port@l40
- `fallbackKind`
- `probeOutput`
- `discoverSerialPortInfo`
- `discoverSerialPorts`
- `path`
- `tioSerialArgs` — tested: :opens a quiet non-reconnecting 8 n1 serial session@l50
- `PosixSerialTransport`
- `PosixSerialTransport.constructor`
- `PosixSerialTransport.on`
- `PosixSerialTransport.emit`
- `PosixSerialTransport.open` — tested: checked in sample parses; corpus excludes ursk; csv appends with one header; firmware format drift; log writes frc sidecar; main cir flag; main reports and writes html; sample exit zero and reports
- `PosixSerialTransport.read` — tested: checked in sample parses; corpus excludes ursk; csv appends with one header; firmware format drift; main reports and writes html; sample exit zero and reports; write cir csv roundtrip
- `PosixSerialTransport.write`
- `PosixSerialTransport.close` — tested: open lines serial

</details>
