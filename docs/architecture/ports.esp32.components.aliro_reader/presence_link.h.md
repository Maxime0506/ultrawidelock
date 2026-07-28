<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/aliro_reader/presence_link.h`

Presence dongle commands (CONFIG_WOZ_PRESENCE): fresh, challenge-driven signed
statements from a new trusted Aliro authentication and later UWB range, turning
proximity of a provisioned iPhone into a factor any tool can check. See
tools/presence_verify.py and tools/presence_git.py for the other end.
These are console commands rather than a private binary channel, so the shell
stays available on the same board: provisioning (aliro-import) and presence both
work without reflashing between modes. Every response is one tagged hex line, so
a log line landing mid-conversation is just another line rather than corruption:
presence pub                 -> PRESENCE-PUB <65 bytes hex>   (enrolment)
presence credential          -> PRESENCE-CRED <8 bytes hex>   (pinned human)
presence prove <nonce-hex>   -> PRESENCE-P256 <115 bytes hex> (fresh proof)
anything rejected            -> PRESENCE-ERR <reason>

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](../ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](../ports.esp32.apps.matter-lock.main/app_shell.cpp.md), [`ports/esp32/apps/reader/main/app_shell.c`](../ports.esp32.apps.reader.main/app_shell.c.md), [`ports/esp32/apps/reader/main/main.c`](../ports.esp32.apps.reader.main/main.c.md), [`ports/esp32/components/aliro_reader/presence_link.c`](presence_link.c.md), [`ports/esp32/components/piv_ccid/piv_identity.c`](../ports.esp32.components.piv_ccid/piv_identity.c.md)
