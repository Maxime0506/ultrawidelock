<!-- generated documentation — edit the source, not this file -->
# `firmware/src/main.c`

**depends on** [`firmware/src/matter_commission.h`](matter_commission.h.md), [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md)

## API

### `static void heap_peak_log(const char *when)`
`firmware/src/main.c:48`

Reported at the grant, because by then the unlock has done every P-256 and
AES-GCM operation it is going to do. The peak is cumulative since boot, so it
covers BLE pairing and the Aliro exchange too, not only the ranging.

**called by** `main`

### `static void provisioning_mode(void)`
`firmware/src/main.c:102`

Runs the console and nothing else. Never returns: leaving this function would
start the radios in a mode the user did not ask for.

**called by** `main`

<details><summary>Undocumented (3)</summary>

- `provisioning_requested`
- `factory_reset_if_requested`
- `main`

</details>
