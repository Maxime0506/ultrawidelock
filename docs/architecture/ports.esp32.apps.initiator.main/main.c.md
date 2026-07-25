<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/initiator/main/main.c`

ESP32-S3 application entry for the Aliro initiator, the User-Device role that
stands in for an iPhone on the bench. Stage 1a wires the BLE transport only: it
starts the NimBLE central, which scans for the reader's 0xFFF2 advert, connects,
reads the reader's SPSM, supported versions and features, writes the version it
selects, and opens the L2CAP channel. It then reports what it learned, including
the BleSK salt those versions imply, and dumps whatever the reader sends. It
stops before AUTH0, because running the transaction needs an Access Credential
the reader trusts and both ends must be provisioned out of band first.

<details><summary>Undocumented (4)</summary>

- `on_ready`
- `on_data`
- `on_closed`
- `app_main`

</details>
