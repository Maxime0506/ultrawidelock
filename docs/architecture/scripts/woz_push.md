<!-- generated documentation — edit the source, not this file -->
# `scripts/woz_push.py`

Push a signed delta patch to a DWM3001CDK over Bluetooth.

    scripts/woz_push.py update.wdfu

The board accepts nothing until an update window is open, so this connects and
then WAITS, asking once a second and prompting you to press SW2. The window
lasts CONFIG_WOZ_DFU_WINDOW_MS, five minutes by default. Start the push first
or press the button first; either order works.

On success the board reboots into MCUboot, which applies the patch -- about
30 seconds during which it is not on the air. The Bluetooth connection dropping
right after COMMIT is the expected ending, not a failure.

Needs bleak:

    python3 -m pip install bleak

WHY GATT AND NOT THE L2CAP CoC the firmware also offers: no Python Bluetooth
library can open an L2CAP connection-oriented channel. CoreBluetooth and BlueZ
both can, bleak wraps neither, and bleak is the only cross-platform option. The
firmware carries both transports for exactly this reason; an iPhone app would
use the CoC and get better throughput.

## API

### `class Session`
`scripts/woz_push.py:53`

One update conversation: write a frame, wait for its reply.

**called by** `run`

#### `Session.call(self, frame, timeout=20.0, tolerate=())`
`scripts/woz_push.py:63`

Send a frame, return the board's byte count.

Errors listed in `tolerate` come back as a negative code instead of
ending the run; everything else is fatal, because there is nothing
useful to do with a board that has refused the transfer.

**called by** `Session.wait_for_window`, `run`  ·  **calls** `die`

#### `Session.wait_for_window(self, total, deadline)`
`scripts/woz_push.py:87`

Retry BEGIN until someone opens the update window.

The board refuses everything until SW2 is pressed, so a push that
started first would otherwise just fail. Asking repeatedly costs the
board a comparison and a two-byte notification: no flash, no state.

**called by** `run`  ·  **calls** `Session.call`, `die`

<details><summary>Undocumented (5)</summary>

- `die`
- `Session.__init__`
- `Session.on_notify`
- `run`
- `main`

</details>
