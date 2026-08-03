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

### `static bool provisioning_requested(void)`
`firmware/src/main.c:90`

Check GPIO SW0 (active-low, pulled up in DTS) to see if provisioning is requested at boot.
Returns true if SW0 is ready and held (logical 1), false otherwise.

**called by** `main`

### `static void provisioning_mode(void)`
`firmware/src/main.c:106`

Runs the console and nothing else. Never returns: leaving this function would
start the radios in a mode the user did not ask for.

**called by** `main`

### `static void factory_reset_if_requested(void)`
`firmware/src/main.c:149`

Check GPIO SW0 (active-low, pulled up in DTS) at boot. If SW0 is held (logical 1), toggle LED0
six times at 120 ms intervals as user feedback, erase Aliro provisioning and Matter fabric (if
CONFIG_ALIRO_MATTER_BLE is on), and log that the board is now commissionable on the next boot.
Returns silently if GPIO is not ready or if SW0 is not held.

**called by** `main`

### `int main(void)`
`firmware/src/main.c:192`

Entry point for the DWM3001CDK reader application. Initializes provisioning and factory-reset
paths, starts the Aliro BLE reader and optional Matter commissioning and DFU receiver, then runs
the approach controller loop. Feeds the controller trusted ranges on each new latch generation
and observes untrusted ranges for departure detection. Grants unlock on approach prediction or
threshold crossing, relocks on departure or abort, and exits with an error code if reader startup
fails.

**calls** `factory_reset_if_requested`, `heap_peak_log`, `provisioning_mode`, `provisioning_requested`
