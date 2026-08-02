<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/initiator/main/main.c`

ESP32-S3 application entry for the Aliro initiator, the User-Device role that
stands in for an iPhone on the bench. Starts the NimBLE central, which scans
for the reader's 0xFFF2 advert, connects, reads the reader's SPSM, supported
versions and features, writes the version it selects, and opens the L2CAP
channel. It then runs the Access Protocol over that channel: every inbound
AUTH0/AUTH1/EXCHANGE command is fed to the device state machine and the sealed
response is framed straight back, ending in the same 32-byte URSK the reader
derives. Credentials are the compiled-in bench pair below, which works only
against a reader running its dev identity with an empty trust store.

<details><summary>Undocumented (5)</summary>

- `phase_str`
- `on_ready`
- `on_data`
- `on_closed`
- `app_main`

</details>
