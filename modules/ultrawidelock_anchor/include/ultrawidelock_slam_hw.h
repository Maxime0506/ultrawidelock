/**
 * @file ultrawidelock_slam_hw.h — LIS2DH12 transport for the impact latch (Zephyr only).
 *
 * Two functions, because that is the whole contract between a door-mounted
 * accelerometer and the 250 ms loop: bring the chip up so it raises a pin on a
 * hard knock, and let the loop ask whether the pin went high since last time.
 *
 * Everything that decides what a strike MEANS lives in ultrawidelock_slam.h, which is
 * pure integer logic and host-tested. This file is the part that cannot be.
 */
#ifndef ULTRAWIDELOCK_SLAM_HW_H
#define ULTRAWIDELOCK_SLAM_HW_H

#include <stdbool.h>

/**
 * Configure the LIS2DH12 to raise INT1 on a high-g transient, and attach a GPIO
 * callback that latches it.
 *
 * Writes the chip's registers directly over I2C rather than instantiating
 * Zephyr's lis2dh driver: see ultrawidelock_slam.h for the measured reason. Verifies
 * WHO_AM_I before writing anything, so a board without the part fails here
 * instead of arming an interrupt that can never fire.
 *
 * @return 0 on success. -ENODEV if the I2C bus or GPIO port is not ready, or if
 *         WHO_AM_I is not 0x33. -EIO on a failed register write. Every failure
 *         leaves the interrupt disabled, and the caller can safely carry on
 *         without a tamper signal.
 */
int ultrawidelock_slam_hw_init(void);

/**
 * Test-and-clear the interrupt latch.
 *
 * @return true if INT1 asserted at least once since the previous call. A burst
 *         of edges between two calls returns true once: the poll interval is
 *         the resolution, by design, and ultrawidelock_slam_poll()'s debounce is sized on
 *         that assumption.
 */
bool ultrawidelock_slam_hw_take(void);

#endif /* ULTRAWIDELOCK_SLAM_HW_H */
