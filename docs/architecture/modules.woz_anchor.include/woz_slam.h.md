<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_slam.h`

@file woz_slam.h — impact and tamper classification from a latched high-g pin.
The LIS2DH12 on the DWM3001CDK can raise INT1 when acceleration crosses a
threshold on any axis. That pin says exactly one thing: "something hit this
door harder than the threshold". It does not say how hard, and it does not
say what. Everything else is inferred here, from timing alone.
WHY THERE IS NO SENSOR DEVICE INVOLVED. Zephyr's lis2dh driver is already
linked into both shipping CDK images and is currently dead weight. Waking it
up means picking a trigger mode, and both cost more than this door can pay:
CONFIG_LIS2DH_TRIGGER_GLOBAL_THREAD puts an ISR bottom half on k_sys_work_q,
which was measured at 3,568 B of its 4,096 B during a live unlock, and
CONFIG_LIS2DH_TRIGGER_OWN_THREAD costs a 1,024 B stack. So the transport half
(woz_slam_lis2dh12.c) writes the chip's registers directly and latches an
atomic from a GPIO callback, and this half turns that latch into meaning from
the 250 ms loop that already runs.
WHAT THIS CANNOT DO, stated so nobody expects it. With no FIFO and no sample
data there is no way to tell a slam from a hard knock by force -- only by
PATTERN. One event is an impact; several inside a window is tamper. A single
very hard knock reads as an impact, and that is correct: the door was struck.
Integer arithmetic over caller-owned structs. No allocation, no threads, no
platform dependency, so the host suite is the whole correctness story.

**used by** [`modules/woz_anchor/src/woz_slam.c`](../modules.woz_anchor.src/woz_slam.c.md)

## API

### `struct woz_slam_cfg`
`modules/woz_anchor/include/woz_slam.h:47`

Tuning. Every field is a time in milliseconds except the count, and every
default below is a starting point to be replaced by a bench capture -- the
number that matters is "louder than closing this door normally", which is a
property of the door and not of the part.

### `struct woz_slam_state`
`modules/woz_anchor/include/woz_slam.h:74`

Classifier state. 24 B of caller-owned .bss; the caller declares it, this
module allocates nothing.
Timestamps are int64_t rather than a 32-bit millisecond counter on purpose.
k_uptime_get() is int64_t, and truncating it puts a wrap at 49.7 days inside
a security signal that is supposed to run for years.
