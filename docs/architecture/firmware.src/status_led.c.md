<!-- generated documentation — edit the source, not this file -->
# `firmware/src/status_led.c`

@file
@brief Show that the update window is open, on the board itself.
The window is the entire authorization model for an update, and until now it
was invisible. Three things open it -- SW2, Apple Home's "Turn On Pairing
Mode", and the bench SWD write -- and none of them gave the board any way to
say so. An owner who pressed the button could not tell whether the press had
registered, and the five minutes could run out while they were still finding
the phone. The only feedback was a log line on a debugger that a released
board does not have attached.
D10, the blue one, at 2 Hz. Blue because the other three are the DW3000's own
colours by convention on this board (D13 is tx red / rx green) and a fourth
red would read as a fault; 2 Hz because a slower heartbeat reads as "alive"
rather than "waiting for you", which is the wrong message for something that
expires.
It follows the window rather than the button, so it is honest about the state
that actually matters: it goes out when the window expires on its own, not
when someone stops pressing.

<details><summary>Undocumented (3)</summary>

- `blink`
- `window_changed`
- `status_led_init`

</details>
