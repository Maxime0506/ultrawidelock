<!-- generated documentation — edit the source, not this file -->
# openaliro

**389 subsystems in 57 directories · 2502/2844 symbols documented (87%)**

**Start here:** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](architecture/modules.woz_aliro.src/aliro_ranging.c.md) — the doors into the codebase (nothing else imports them).

```mermaid
flowchart LR
  host.presence --> tools
  integration.homeassistant --> tools.tui.src
  integration.homeassistant.src.openaliro_ha --> tools.tui.src
  modules.woz_aliro.include --> modules.woz_aliro.src
  modules.woz_aliro.src --> modules.woz_aliro.include
  modules.woz_aliro.src --> modules.woz_ml.include
  modules.woz_aliro.src --> modules.woz_port.include
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_aliro.src --> modules.woz_uwb.src.facade
  modules.woz_aliro_stack.src --> modules.woz_aliro_stack.src.protocol
  modules.woz_anchor.src --> modules.woz_anchor.include
  modules.woz_anchor.src --> modules.woz_port.include
  modules.woz_dfu.src --> modules.woz_dfu.include
  modules.woz_matter.src --> modules.woz_aliro.src
  modules.woz_matter.src --> modules.woz_matter.include
  modules.woz_ml.src --> modules.woz_ml.include
  modules.woz_nfc.src --> modules.woz_nfc.include.woz_nfc
  modules.woz_uwb.src.aliro --> modules.woz_port.include
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.aliro.include.aliro_uwb_adapter --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_port.include
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.driver --> modules.woz_port.include
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.facade --> modules.woz_port.include
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.fira --> modules.woz_port.include
  modules.woz_uwb.src.fira --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.fira
  ports.esp32.apps.matter-lock.main --> ports.esp32.apps.matter-lock.main.lock
  ports.esp32.apps.matter-lock.main --> ports.esp32.components.aliro_reader
  ports.esp32.apps.matter-lock.main --> ports.esp32.components.piv_ccid.include
  ports.esp32.apps.reader.main --> ports.esp32.components.aliro_reader
  ports.esp32.components.piv_ccid --> ports.esp32.components.aliro_reader
  ports.esp32.components.piv_ccid --> ports.esp32.components.piv_ccid.include
  tools --> tools.tui.src
```

## Directories

| directory | subsystems | documented |
|---|---|---|
| [`activity/`](architecture/activity/README.md) | 1 | 2/4 (50%) |
| [`activity/scripts/`](architecture/activity.scripts/README.md) | 5 | 0/9 (0%) |
| [`activity/src/`](architecture/activity.src/README.md) | 2 | 2/8 (25%) |
| [`ai/tinyml/`](architecture/ai.tinyml/README.md) | 11 | 48/75 (64%) |
| [`anchor/src/`](architecture/anchor.src/README.md) | 3 | 20/20 (100%) |
| [`firmware/src/`](architecture/firmware.src/README.md) | 18 | 193/206 (93%) |
| [`host/presence/`](architecture/host.presence/README.md) | 5 | 36/36 (100%) |
| [`integration/homeassistant/`](architecture/integration.homeassistant/README.md) | 1 | 5/5 (100%) |
| [`integration/homeassistant/custom_components/openaliro/`](architecture/integration.homeassistant.custom_components.openaliro/README.md) | 8 | 31/34 (91%) |
| [`integration/homeassistant/scripts/`](architecture/integration.homeassistant.scripts/README.md) | 1 | 3/3 (100%) |
| [`integration/homeassistant/src/openaliro_ha/`](architecture/integration.homeassistant.src.openaliro_ha/README.md) | 11 | 109/120 (90%) |
| [`integration/homeassistant/tools/`](architecture/integration.homeassistant.tools/README.md) | 1 | 3/3 (100%) |
| [`modules/woz_aliro/include/`](architecture/modules.woz_aliro.include/README.md) | 16 | 38/38 (100%) |
| [`modules/woz_aliro/src/`](architecture/modules.woz_aliro.src/README.md) | 21 | 273/276 (98%) |
| [`modules/woz_aliro_ecp/src/`](architecture/modules.woz_aliro_ecp.src/README.md) | 1 | 5/5 (100%) |
| [`modules/woz_aliro_stack/src/`](architecture/modules.woz_aliro_stack.src/README.md) | 4 | 77/77 (100%) |
| [`modules/woz_aliro_stack/src/protocol/`](architecture/modules.woz_aliro_stack.src.protocol/README.md) | 14 | 67/67 (100%) |
| [`modules/woz_anchor/include/`](architecture/modules.woz_anchor.include/README.md) | 6 | 9/9 (100%) |
| [`modules/woz_anchor/src/`](architecture/modules.woz_anchor.src/README.md) | 6 | 8/41 (19%) |
| [`modules/woz_dfu/include/`](architecture/modules.woz_dfu.include/README.md) | 2 | 1/1 (100%) |
| [`modules/woz_dfu/scripts/`](architecture/modules.woz_dfu.scripts/README.md) | 1 | 1/1 (100%) |
| [`modules/woz_dfu/src/`](architecture/modules.woz_dfu.src/README.md) | 3 | 42/48 (87%) |
| [`modules/woz_matter/include/`](architecture/modules.woz_matter.include/README.md) | 16 | 44/44 (100%) |
| [`modules/woz_matter/src/`](architecture/modules.woz_matter.src/README.md) | 14 | 211/219 (96%) |
| [`modules/woz_ml/include/`](architecture/modules.woz_ml.include/README.md) | 2 | 1/1 (100%) |
| [`modules/woz_ml/src/`](architecture/modules.woz_ml.src/README.md) | 10 | 3/15 (20%) |
| [`modules/woz_nfc/include/woz_nfc/`](architecture/modules.woz_nfc.include.woz_nfc/README.md) | 1 | 1/1 (100%) |
| [`modules/woz_nfc/src/`](architecture/modules.woz_nfc.src/README.md) | 9 | 64/64 (100%) |
| [`modules/woz_port/include/`](architecture/modules.woz_port.include/README.md) | 2 | 12/12 (100%) |
| [`modules/woz_uwb/src/aliro/`](architecture/modules.woz_uwb.src.aliro/README.md) | 12 | 92/92 (100%) |
| [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/README.md) | 2 | 4/4 (100%) |
| [`modules/woz_uwb/src/aliro/include/cherry/`](architecture/modules.woz_uwb.src.aliro.include.cherry/README.md) | 4 | 13/13 (100%) |
| [`modules/woz_uwb/src/ccc/`](architecture/modules.woz_uwb.src.ccc/README.md) | 17 | 123/123 (100%) |
| [`modules/woz_uwb/src/driver/`](architecture/modules.woz_uwb.src.driver/README.md) | 9 | 55/56 (98%) |
| [`modules/woz_uwb/src/facade/`](architecture/modules.woz_uwb.src.facade/README.md) | 12 | 88/88 (100%) |
| [`modules/woz_uwb/src/fira/`](architecture/modules.woz_uwb.src.fira/README.md) | 5 | 19/19 (100%) |
| [`modules/woz_uwb/src/shell/`](architecture/modules.woz_uwb.src.shell/README.md) | 2 | 14/14 (100%) |
| [`ports/esp32/apps/initiator/main/`](architecture/ports.esp32.apps.initiator.main/README.md) | 1 | 3/5 (60%) |
| [`ports/esp32/apps/matter-lock/main/`](architecture/ports.esp32.apps.matter-lock.main/README.md) | 9 | 58/58 (100%) |
| [`ports/esp32/apps/matter-lock/main/lock/`](architecture/ports.esp32.apps.matter-lock.main.lock/README.md) | 5 | 60/60 (100%) |
| [`ports/esp32/apps/reader/main/`](architecture/ports.esp32.apps.reader.main/README.md) | 3 | 22/22 (100%) |
| [`ports/esp32/components/aliro_ble/`](architecture/ports.esp32.components.aliro_ble/README.md) | 1 | 42/42 (100%) |
| [`ports/esp32/components/aliro_ble_central/`](architecture/ports.esp32.components.aliro_ble_central/README.md) | 1 | 14/18 (77%) |
| [`ports/esp32/components/aliro_reader/`](architecture/ports.esp32.components.aliro_reader/README.md) | 4 | 19/21 (90%) |
| [`ports/esp32/components/piv_ccid/`](architecture/ports.esp32.components.piv_ccid/README.md) | 4 | 40/60 (66%) |
| [`ports/esp32/components/piv_ccid/include/`](architecture/ports.esp32.components.piv_ccid.include/README.md) | 4 | 3/3 (100%) |
| [`ports/esp32/components/woz_uwb/port/`](architecture/ports.esp32.components.woz_uwb.port/README.md) | 4 | 30/30 (100%) |
| [`ports/nrf5340dk/initiator/src/`](architecture/ports.nrf5340dk.initiator.src/README.md) | 5 | 18/23 (78%) |
| [`ports/nrf5340dk/on_target_ec/src/`](architecture/ports.nrf5340dk.on_target_ec.src/README.md) | 1 | 1/1 (100%) |
| [`release/dwm3001cdk/`](architecture/release.dwm3001cdk/README.md) | 1 | 0/0 (0%) |
| [`release/esp32-matter-lock/`](architecture/release.esp32-matter-lock/README.md) | 1 | 0/0 (0%) |
| [`release/nrf5340dk/`](architecture/release.nrf5340dk/README.md) | 1 | 0/0 (0%) |
| [`scripts/`](architecture/scripts/README.md) | 41 | 162/209 (77%) |
| [`tools/`](architecture/tools/README.md) | 29 | 235/267 (88%) |
| [`tools/tui/src/`](architecture/tools.tui.src/README.md) | 12 | 51/142 (35%) |
| [`web-flasher/`](architecture/web-flasher/README.md) | 1 | 8/8 (100%) |
| [`web-twin/`](architecture/web-twin/README.md) | 3 | 19/24 (79%) |

## Hotspots

*Mined from git history as of `8b9de4e4`.*

**Most-changed:** [`modules/woz_aliro/src/aliro_reader.c`](architecture/modules.woz_aliro.src/aliro_reader.c.md) (25 commits), [`modules/woz_matter/src/matter_clusters.c`](architecture/modules.woz_matter.src/matter_clusters.c.md) (24 commits), [`scripts/verify.sh`](architecture/scripts/verify.sh.md) (24 commits), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md) (23 commits), [`modules/woz_matter/include/matter_clusters.h`](architecture/modules.woz_matter.include/matter_clusters.h.md) (20 commits).

**Change together without importing each other:**

- [`modules/woz_matter/include/matter_im.h`](architecture/modules.woz_matter.include/matter_im.h.md) ↔ [`modules/woz_matter/src/matter_clusters.c`](architecture/modules.woz_matter.src/matter_clusters.c.md) (7 shared commits)
- [`modules/woz_uwb/src/shell/aliro_shell.c`](architecture/modules.woz_uwb.src.shell/aliro_shell.c.md) ↔ [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md) (6 shared commits)
- [`modules/woz_matter/include/matter_clusters.h`](architecture/modules.woz_matter.include/matter_clusters.h.md) ↔ [`modules/woz_matter/src/matter_im.c`](architecture/modules.woz_matter.src/matter_im.c.md) (5 shared commits)
- [`modules/woz_matter/src/matter_clusters.c`](architecture/modules.woz_matter.src/matter_clusters.c.md) ↔ [`modules/woz_matter/src/matter_im.c`](architecture/modules.woz_matter.src/matter_im.c.md) (5 shared commits)
- [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](architecture/modules.woz_uwb.src.facade/uwb_cirdiag.h.md) ↔ [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](architecture/ports.esp32.apps.matter-lock.main/app_shell.cpp.md) (5 shared commits)
