<!-- generated documentation — edit the source, not this file -->
# `firmware/src/status_led.h`

@file
@brief The four board LEDs as one state display, and the only way to drive them.
Every LED on this board goes through status_led_signal(). Nothing else may
touch led0..led3: two owners toggling the same pin from a work queue and a
loop produce a light that flickers between two truths, which is worse than no
light at all.
A signal is a fact about the board, not a blink rate. Callers say what is
true; src/status_led.c decides which LED shows it and how. That split is what
lets the whole display be re-mapped in one function instead of in five call
sites, and it is why the callers below can be one line each.
Safe from any task: the setter is one atomic store and a work submit, so it
can be called from the BLE host task, from OpenThread, or from the reader
loop between ranging rounds without taking a lock or blocking. It must never
be called from the DW3110 callbacks themselves -- nothing may be, the arm
deadline there is ~1836 us -- but the 250 ms reader loop is fine.

**used by** [`firmware/src/main.c`](main.c.md), [`firmware/src/matter_commission.c`](matter_commission.c.md), [`firmware/src/status_led.c`](status_led.c.md)

<details><summary>Undocumented (2)</summary>

- `status_led_signal`
- `status_led_boot_blink`

</details>
