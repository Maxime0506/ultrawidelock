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

## API

### `static const char *phase_str(enum aliro_device_phase p)`
`ports/esp32/apps/initiator/main/main.c:97`

Return a human-readable string for an Aliro device phase: IDLE, SENT_AUTH0_RESP, SENT_AUTH1_RESP,
ESTABLISHED, or FAILED.

**called by** `on_closed`, `on_data`

### `static void on_ready(uint16_t conn_handle, const struct aliro_ble_central_peer *peer)`
`ports/esp32/apps/initiator/main/main.c:119`

Callback when the BLE transport is ready after the peer advertises its SPSM and supported
versions. Initialize the device state machine with the credential private key and reader
identity, derive the BleSK salt from the peer's published version list, and arm the device to
wait for AUTH0. Log connection and version details.

### `void app_main(void)`
`ports/esp32/apps/initiator/main/main.c:241`

Initialize the Aliro BLE central stack, register event callbacks for ready/data/closed, bring up
the PSA crypto backend, and begin scanning for an Aliro reader. Run forever, yielding
periodically.

<details><summary>Undocumented (2)</summary>

- `on_data`
- `on_closed`

</details>
