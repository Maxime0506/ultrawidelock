# Stage 0 hardware matrix

Record one reviewed result per claimed installation type. Keep the raw USB
serial number, device path, hostname, and console archive out of this file.
Use a local board pseudonym instead.

| Installation type | Board pseudonym | VCOM1 metadata recorded | Open and command probe | Board-reset reopen | HA-restart reopen | Result |
| --- | --- | --- | --- | --- | --- | --- |
| Home Assistant OS | `<board_a>` | Pending | Pending | Pending | Pending | Pending |
| Home Assistant Container | `<board_a>` | Pending | Pending | Pending | Pending | Pending |
| Home Assistant Core | `<board_a>` | Pending | Pending | Pending | Pending | Pending |

For every completed row, link the sanitized capture IDs from
`captures/manifest.json` in the review record. Record the USB vendor ID,
product ID, interface number, and non-sensitive product text there. A complete
row must distinguish the active VCOM1 interface from the silent interface and
must prove a single board remains one logical device after reconnect.
