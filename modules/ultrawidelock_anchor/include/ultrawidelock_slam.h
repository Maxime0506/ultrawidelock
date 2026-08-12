/**
 * @file ultrawidelock_slam.h — impact and tamper classification from a latched high-g pin.
 *
 * The LIS2DH12's INT1 says exactly one thing: "something hit this door harder
 * than the threshold" -- not how hard, not what. Everything else is inferred
 * here from timing alone. No Zephyr sensor device: both trigger modes cost RAM
 * this image does not have, so the transport half (ultrawidelock_slam_lis2dh12.c) writes
 * the chip's registers directly and latches an atomic from a GPIO callback,
 * which this half turns into meaning from the 250 ms loop that already runs.
 * With no FIFO there is no force, only PATTERN: one event is an impact, several
 * inside a window is tamper; a single very hard knock reads as an impact, which
 * is correct -- the door was struck. Integer arithmetic over caller-owned
 * structs, no allocation, no platform dependency.
 */
#ifndef ULTRAWIDELOCK_SLAM_H
#define ULTRAWIDELOCK_SLAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** What the classifier concluded on this poll. */
enum ultrawidelock_slam_event {
	ULTRAWIDELOCK_SLAM_NONE = 0, /**< nothing accepted this poll */
	ULTRAWIDELOCK_SLAM_IMPACT,   /**< one accepted strike */
	ULTRAWIDELOCK_SLAM_TAMPER,   /**< enough strikes inside the window to mean intent */
};

/**
 * Tuning. Every field is a time in milliseconds except the count, and every
 * default below is a starting point to be replaced by a bench capture -- the
 * number that matters is "louder than closing this door normally", which is a
 * property of the door and not of the part.
 */
struct ultrawidelock_slam_cfg {
	/**
	 * Ignore re-triggers within this of the last accepted strike. A real
	 * impact rings: the leaf rebounds, the latch chatters, and the pin
	 * fires several times for one event. Without this every slam is
	 * instant tamper.
	 */
	uint32_t debounce_ms;
	/** Strikes inside this window are treated as one burst. */
	uint32_t tamper_window_ms;
	/** This many accepted strikes inside the window latches tamper. */
	uint8_t tamper_count;
};

/** Defaults. See the cfg comments for why each is a placeholder, not a value. */
#define ULTRAWIDELOCK_SLAM_DEBOUNCE_MS_DEFAULT      400u
#define ULTRAWIDELOCK_SLAM_TAMPER_WINDOW_MS_DEFAULT 4000u
#define ULTRAWIDELOCK_SLAM_TAMPER_COUNT_DEFAULT     3u

/**
 * Classifier state. 24 B of caller-owned .bss; the caller declares it, this
 * module allocates nothing.
 *
 * Timestamps are int64_t rather than a 32-bit millisecond counter on purpose.
 * k_uptime_get() is int64_t, and truncating it puts a wrap at 49.7 days inside
 * a security signal that is supposed to run for years.
 */
struct ultrawidelock_slam_state {
	int64_t last_accept_ms;  /**< when the last strike was accepted */
	int64_t window_start_ms; /**< when the current burst window opened */
	uint8_t window_count;    /**< accepted strikes inside that window */
	bool tamper_latched;     /**< tamper reported and not yet cleared */
	bool seeded;             /**< false until the first strike; see ultrawidelock_slam_poll */
};

/** Reset to "nothing has ever hit this door". */
void ultrawidelock_slam_init(struct ultrawidelock_slam_state *s);

/**
 * Fold one poll of the interrupt latch into the state.
 *
 * @param cfg        Tuning; NULL or a zero tamper_count yields ULTRAWIDELOCK_SLAM_NONE.
 * @param s          Caller-owned state, previously ultrawidelock_slam_init()ed.
 * @param struck     True if the GPIO callback latched at least one edge since
 *                   the previous poll. The caller is expected to test-and-clear
 *                   its atomic, so a burst arriving between two polls is one
 *                   `true` and is deliberately counted once: the poll rate is
 *                   the debounce floor.
 * @param now_ms     Monotonic milliseconds, k_uptime_get() on target.
 * @return ULTRAWIDELOCK_SLAM_TAMPER on the poll that latches tamper, ULTRAWIDELOCK_SLAM_IMPACT on
 *         an accepted strike, ULTRAWIDELOCK_SLAM_NONE otherwise. TAMPER is returned once
 *         per latch, not once per subsequent strike, so a caller that logs it
 *         cannot be made to log forever by continuing to hit the door.
 */
enum ultrawidelock_slam_event ultrawidelock_slam_poll(const struct ultrawidelock_slam_cfg *cfg,
						      struct ultrawidelock_slam_state *s,
						      bool struck, int64_t now_ms);

/** True while tamper is latched. Survives until ultrawidelock_slam_clear_tamper(). */
bool ultrawidelock_slam_tampered(const struct ultrawidelock_slam_state *s);

/**
 * Clear the tamper latch. Deliberately explicit rather than time-based: a door
 * that was being attacked should stay flagged until something decides the
 * incident is over, and that decision does not belong to a 250 ms loop.
 */
void ultrawidelock_slam_clear_tamper(struct ultrawidelock_slam_state *s);

#endif /* ULTRAWIDELOCK_SLAM_H */
