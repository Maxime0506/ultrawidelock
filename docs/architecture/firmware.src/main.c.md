<!-- generated documentation — edit the source, not this file -->
# `firmware/src/main.c`

**depends on** [`firmware/src/matter_commission.h`](matter_commission.h.md), [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md), [`firmware/src/status_led.h`](status_led.h.md)  ·  **discussed in** [`ai/README.md`](../../../ai/README.md)

## API

### `static void heap_peak_log(const char *when)`
`firmware/src/main.c:63`

Reported at the grant, because by then the unlock has done every P-256 and
AES-GCM operation it is going to do. The peak is cumulative since boot, so it
covers BLE pairing and the Aliro exchange too, not only the ranging.

**called by** `main`

### `static void on_range_latched(void)`
`firmware/src/main.c:116`

Wake the grant loop on an accepted range latch. Runs on the UWB RX path, so it does nothing but
give the semaphore -- the float math in the approach controller stays on the main thread.

### `static bool provisioning_requested(void)`
`firmware/src/main.c:137`

Check GPIO SW0 (active-low, pulled up in DTS) to see if provisioning is requested at boot.
Returns true if SW0 is ready and held (logical 1), false otherwise.

**called by** `main`

### `static void provisioning_mode(void)`
`firmware/src/main.c:153`

Runs the console and nothing else. Never returns: leaving this function would
start the radios in a mode the user did not ask for.

**called by** `main`

### `static void factory_reset_if_requested(void)`
`firmware/src/main.c:201`

Check GPIO SW0 (active-low, pulled up in DTS) at boot. If SW0 is held (logical 1), blink the lock
LED as user feedback, erase Aliro provisioning and Matter fabric (if CONFIG_ALIRO_MATTER_BLE is
on), and log that the board is now commissionable on the next boot. Returns silently if GPIO is
not ready or if SW0 is not held.

**called by** `main`

### `static enum aliro_approach_action feed_with_channel(struct aliro_approach *ap, int64_t now, int32_t cm)`
`firmware/src/main.c:253`

Feed one trusted range, carrying this reception's channel class if there is one.
WHERE THIS RUNS, because it is the only reason it is affordable. The classifier
needs dwt_readdiagnostics(), measured at 972 us on this board -- 53% of the
~1836 us ranging arm deadline, which would be reckless on the RX path. It is
not on the RX path. uwb_cirdiag_capture() already takes that read AFTER the
shim re-arms, and this function only copies the result out in the main loop,
one ranging block (~192 ms) later. The work added here is five register copies,
three logarithms and two comparisons.
The channel is read only when the capture counter has ADVANCED. The latch is
latest-wins with no queue, so a stale snapshot re-read across several ranging
rounds would let one obstructed reception carry a whole median window and
defeat the majority-of-five that gates the widening.
Falls back to the plain feed whenever anything is missing -- stream disarmed,
nothing captured, a failed CIA read, or the classifier compiled out. A missing
class must read as CLEAR rather than as obstructed: clear is the unwidened
threshold, which is the behaviour that shipped.

**called by** `main`

### `int main(void)`
`firmware/src/main.c:304`

Entry point for the DWM3001CDK reader application. Initializes provisioning and factory-reset
paths, starts the Aliro BLE reader and optional Matter commissioning and DFU receiver, then runs
the approach controller loop. Feeds the controller trusted ranges on each new latch generation
and observes untrusted ranges for departure detection. Grants unlock on approach prediction or
threshold crossing, relocks on departure or abort, and exits with an error code if reader startup
fails.

**calls** `factory_reset_if_requested`, `feed_with_channel`, `heap_peak_log`, `provisioning_mode`, `provisioning_requested`
