<!-- generated documentation — edit the source, not this file -->
# `firmware/src/thread_gate.c`

## API

### `static void on_state_changed(otChangedFlags flags, void *context)`
`firmware/src/thread_gate.c:116`

OpenThread state change callback. Logs the device role when it changes and, if the node becomes a
child or router, logs its mesh-local EID so a remote peer can discover the address to send
unicast traffic to.

### `static void gate_beat(void *a, void *b, void *c)`
`firmware/src/thread_gate.c:172`

Periodic thread status log. Runs every GATE_BEAT_MS milliseconds and logs the OpenThread device
role and MAC counters (total RX and TX frames). Runs on a low-priority background thread.

### `static int thread_gate_start(void)`
`firmware/src/thread_gate.c:202`

Start the Thread contention gate. Initializes OpenThread with a fixed operational dataset on the
configured channel and PAN ID, brings up the Thread network interface, starts the OpenThread run
loop, and spawns a periodic log thread. Returns 0 on success or negative on init, interface, or
OpenThread run failure.
