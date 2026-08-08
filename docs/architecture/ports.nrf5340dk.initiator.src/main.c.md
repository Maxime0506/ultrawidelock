<!-- generated documentation — edit the source, not this file -->
# `ports/nrf5340dk/initiator/src/main.c`

nRF5340 DK application entry for the Aliro initiator, the User-Device role that
stands in for an iPhone on the bench. Starts the Zephyr BLE central, which
scans for the reader's 0xFFF2 advert, connects, reads the reader's SPSM,
supported versions and features, writes the version it selects, and opens the
L2CAP channel. It then runs the Access Protocol over that channel: every
inbound AUTH0/AUTH1/EXCHANGE command is fed to the device state machine and the
sealed response is framed straight back, ending in the same 32-byte URSK the
reader derives. Credentials are the compiled-in bench pair below, which works
only against a reader running its dev identity with an empty trust store.

**depends on** [`ports/nrf5340dk/initiator/src/ranging.h`](ranging.h.md)

## API

### `static int hfclk_div_fixup(void)`
`ports/nrf5340dk/initiator/src/main.c:73`

Put HFCLK on DIV_1 before anything else starts, so SPIM4 samples MISO correctly.
Measured on this bench 2026-08-07. With HFCLKCTRL at DIV_2, every DW3000 read
comes back sampled one SPI clock early: DEV_ID 0xdeca0302 arrives as
0x6fe50101, which reconstructs exactly under a one-bit right shift with
byte-to-byte carry. Setting DIV_1 turns the same read good on the same board
with nothing else changed, so the divider is causal rather than correlated.
SPIM4's sample point is derived from HFCLK; spi_nrfx_spim.c:182-184 reads this
same divider for the same reason.
Why this is not inherited: nrf_qspi_nor.c is the ONLY caller of
nrf_clock_hfclk_div_set() in Zephyr and NCS combined. The door lock reads the
DW3000 correctly purely because it builds CONFIG_NORDIC_QSPI_NOR for its
external flash and that driver leaves the divider on DIV_1. This application
has no external flash and must therefore own the divider itself. Do not
"clean this up" by deleting it because nothing appears to use it.
PRE_KERNEL_1 so it lands before the SPI driver, and well before BLE and MPSL,
which is the reason it is here and not in radio_probe(): changing a global
clock underneath a running radio is how you buy intermittent faults.

### `static const char *phase_str(enum aliro_device_phase p)`
`ports/nrf5340dk/initiator/src/main.c:163`

Return a human-readable string for an Aliro device phase: IDLE, SENT_AUTH0_RESP, SENT_AUTH1_RESP,
ESTABLISHED, or FAILED.

**called by** `on_closed`, `on_data`

### `static void on_ready(uint16_t conn_handle, const struct aliro_ble_central_peer *peer)`
`ports/nrf5340dk/initiator/src/main.c:185`

Callback when the BLE transport is ready after the peer advertises its SPSM and supported
versions. Initialize the device state machine with the credential private key and reader
identity, derive the BleSK salt from the peer's published version list, and arm the device to
wait for AUTH0. Log connection and version details.

### `int main(void)`
`ports/nrf5340dk/initiator/src/main.c:380`

Initialize the Aliro BLE central stack, register event callbacks for ready/data/closed, bring up
the PSA crypto backend, and begin scanning for an Aliro reader.

**calls** `radio_probe`

<details><summary>Undocumented (3)</summary>

- `on_data`
- `on_closed`
- `radio_probe`

</details>
