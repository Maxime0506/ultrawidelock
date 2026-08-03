<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-dfu.sh`

cdk-dfu.sh — push a signed image to the DWM3001CDK over MCUboot serial recovery.
WHY THIS IS A SCRIPT AND NOT A MAKE RECIPE. The ordering below is the whole
job and a recipe got it wrong twice. MCUboot listens for an mcumgr command for
a fixed window after reset and then boots the application; miss the window and
the port answers nothing, which looks exactly like broken wiring. So the probe
loop has to already be RUNNING when the reset lands. Backgrounding the reset
instead does not work: nrfjprog spends seconds connecting to the probe before
it pulls the line, by which time the window has opened and shut.
NO BUTTON IS NEEDED, but not for the reason an earlier version of this comment
gave. SW1 DOES reset this board: UICR.PSELRESET reads 0x00000012 (pin 18,
CONNECT clear), and a tap produces a full fresh boot on RTT. Check it with
nrfjprog --memrd 0x10001200 --n 8
CONFIG_GPIO_AS_PINRESET only WRITES that field, so grepping an app's .config
for that symbol says nothing about whether the pin currently resets.
This resets over SWD anyway, because that needs no operator and no timing.
WHAT DOES NOT WORK, AND IS NOT UNDERSTOOD. Serial recovery completed exactly
one real upload (2026-08-02 ~22:00) and has not been reproducible since, on
the same config, binary and board. Ruled out by measurement, none of them the
cause: the window duration (400 ms and 30000 ms fail identically), a J-Link
session blocking the VCOM (a cold boot with no debugger fails too), a stale
process on the port, a wedged probe, board health, the provisioned state, and
the reset mechanism. Also verified WORKING: UART TX (3,392 bytes out of the
app), UART RX electrically (EVENTS_RXDRDY=1 and ERRORSRC=0x1 after 200 bytes
in), the pinctrl in both images, and MCUboot reaching its wait window at all
(the application appears ~5 s after reset, which is the window elapsing).
So MCUboot sits in its window on a working UART and does not answer. The next
measurement worth taking is instrumenting MCUboot itself rather than inferring
it from outside: CONFIG_MCUBOOT_INDICATION_LED with an mcuboot-led0 alias, or
logging over RTT, to see whether boot_serial_check_start is entered and with
what timeout.

**discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `probe()`
`scripts/cdk-dfu.sh:59`

Probe device connectivity by listing MCUmgr images over serial connection with 0.4 second timeout.
Returns true if device responds, false otherwise.
