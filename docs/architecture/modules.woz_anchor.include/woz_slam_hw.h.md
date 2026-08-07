<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/include/woz_slam_hw.h`

@file woz_slam_hw.h — LIS2DH12 transport for the impact latch (Zephyr only).
Two functions, because that is the whole contract between a door-mounted
accelerometer and the 250 ms loop: bring the chip up so it raises a pin on a
hard knock, and let the loop ask whether the pin went high since last time.
Everything that decides what a strike MEANS lives in woz_slam.h, which is
pure integer logic and host-tested. This file is the part that cannot be.

**used by** [`modules/woz_anchor/src/woz_slam_lis2dh12.c`](../modules.woz_anchor.src/woz_slam_lis2dh12.c.md)
