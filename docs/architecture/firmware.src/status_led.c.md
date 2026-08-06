<!-- generated documentation — edit the source, not this file -->
# `firmware/src/status_led.c`

@file
@brief The four board LEDs as one state display.
This board has no console. RTT needs probe-rs and the ELF that was actually
flashed, uart0 belongs to the J-Link OB, and the USB console only exists in
provisioning mode -- so on a board doing its job, four LEDs are the entire
output. They used to carry one bit between them: D10 blinked while the update
window was open, and a lock that unlocked looked exactly like a lock that had
hung.
WHAT EACH LED MEANS. One LED per question, so no two facts ever contend for
the same lamp and nothing has to be decoded from a rate alone:
D9  green  the lock      solid = unlocked · one blip per 2 s = locked, alive
D12 red    attention     solid = fault · 0.5 Hz = no fabric, needs commissioning
D11 red    the phone     4 Hz = ranging · 1 Hz = Aliro session · off = idle
D10 blue   a window      2 Hz = update window open · solid = provisioning mode
D13 is not ours: the DW3110 drives it directly as tx red / rx green, and D20
belongs to the J-Link OB.
WHY GREEN IS SOLID WHEN OPEN. The lock LED is on for the state that should
pull someone's eye across a room, and the unlocked state is that state. The
one-blip idle is the other half of the same argument: without it, "locked" and
"the firmware died" are the same picture, and on a board with no console that
is the ambiguity that costs the most time.
WHY A PATTERN TABLE. Each LED renders a 16-slot bit pattern at 125 ms a slot,
so every rate this display can show is one 16-bit literal and the whole
schedule is a single timer. Adding a rate costs a constant, not a timer, on a
part with 6 KB of RAM left. The tick stops itself when every pattern is static
(all-on or all-off), so an idle locked board with the heartbeat compiled out
costs nothing at all.
NOTHING HERE BLOCKS except status_led_boot_blink(), which says so. The tick
handler does four GPIO writes and reschedules; it runs on the system work
queue, where the existing update-window blink already ran, and it must stay
that cheap -- the DW3110 reply arm deadline is ~1836 us and this fires eight
times a second.

**depends on** [`firmware/src/status_led.h`](status_led.h.md)

## API

### `static void render(uint32_t sig, uint16_t pat[LED_COUNT])`
`firmware/src/status_led.c:118`

Map the asserted signals onto one pattern per LED.
The whole display is re-mapped here and nowhere else. Within an LED the tests
are ordered most-specific first, which is also most-urgent first: ranging
cannot happen without a session, and a fault outranks a board that merely
wants commissioning.

**called by** `tick`

### `static void tick(struct k_work *work)`
`firmware/src/status_led.c:144`

Drive every LED to its level for the current slot, advance the phase, and
reschedule only if something still moves.
Stopping on an all-static frame is what keeps an idle board off this timer:
a static pattern has the same level in all sixteen slots, so the levels just
written are already correct for every slot that will not now happen, and the
next status_led_signal() restarts the tick.

**calls** `render`

### `void status_led_signal(uint32_t signals, bool on)`
`firmware/src/status_led.c:168`

Assert (@p on true) or clear @p signals, which may be several ORed together.
Idempotent and cheap: a call that changes nothing costs one compare and
returns. A call that does change something restarts the blink phase, so every
transition lights within a millisecond rather than up to one slot later --
the quarter second of nothing after a button press is exactly the gap that
makes someone press again.

**called by** `window_changed`

### `void status_led_boot_blink(void)`
`firmware/src/status_led.c:195`

Blink the lock LED six times over ~720 ms and return, for the one piece of
feedback that has to be given before the thing it confirms happens: the
factory-reset button, whose erase follows immediately.
Blocking on purpose, and the only blocking call here. It runs from main()
before the radios start, where 720 ms costs nothing and an asynchronous
signal would be overwritten by the erase's own state changes.

### `static void window_changed(bool open)`
`firmware/src/status_led.c:227`

Follow the update window rather than the button that opened it, so the light
goes out when the five minutes expire on their own.

**calls** `status_led_signal`

### `static int status_led_init(void)`
`firmware/src/status_led.c:241`

Configure every LED the board actually has, subscribe to the update window if
this image has a DFU receiver, and start the display.
APPLICATION level: the GPIO driver is up by then, and nothing can assert a
signal before the radios start anyway. Never fails the boot -- a board that
will not blink must still unlock a door.
