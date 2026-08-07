/**
 * @file woz_slam.h — impact and tamper classification from a latched high-g pin.
 *
 * The LIS2DH12 on the DWM3001CDK can raise INT1 when acceleration crosses a
 * threshold on any axis. That pin says exactly one thing: "something hit this
 * door harder than the threshold". It does not say how hard, and it does not
 * say what. Everything else is inferred here, from timing alone.
 *
 * WHY THERE IS NO SENSOR DEVICE INVOLVED. Zephyr's lis2dh driver is already
 * linked into both shipping CDK images and is currently dead weight. Waking it
 * up means picking a trigger mode, and both cost more than this door can pay:
 * CONFIG_LIS2DH_TRIGGER_GLOBAL_THREAD puts an ISR bottom half on k_sys_work_q,
 * which was measured at 3,568 B of its 4,096 B during a live unlock, and
 * CONFIG_LIS2DH_TRIGGER_OWN_THREAD costs a 1,024 B stack. So the transport half
 * (woz_slam_lis2dh12.c) writes the chip's registers directly and latches an
 * atomic from a GPIO callback, and this half turns that latch into meaning from
 * the 250 ms loop that already runs.
 *
 * WHAT THIS CANNOT DO, stated so nobody expects it. With no FIFO and no sample
 * data there is no way to tell a slam from a hard knock by force -- only by
 * PATTERN. One event is an impact; several inside a window is tamper. A single
 * very hard knock reads as an impact, and that is correct: the door was struck.
 *
 * Integer arithmetic over caller-owned structs. No allocation, no threads, no
 * platform dependency, so the host suite is the whole correctness story.
 */
#ifndef WOZ_SLAM_H
#define WOZ_SLAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** What the classifier concluded on this poll. */
enum woz_slam_event {
	WOZ_SLAM_NONE = 0, /**< nothing accepted this poll */
	WOZ_SLAM_IMPACT,   /**< one accepted strike */
	WOZ_SLAM_TAMPER,   /**< enough strikes inside the window to mean intent */
};

/**
 * Tuning. Every field is a time in milliseconds except the count, and every
 * default below is a starting point to be replaced by a bench capture -- the
 * number that matters is "louder than closing this door normally", which is a
 * property of the door and not of the part.
 */
struct woz_slam_cfg {
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
#define WOZ_SLAM_DEBOUNCE_MS_DEFAULT      400u
#define WOZ_SLAM_TAMPER_WINDOW_MS_DEFAULT 4000u
#define WOZ_SLAM_TAMPER_COUNT_DEFAULT     3u

/**
 * Classifier state. 24 B of caller-owned .bss; the caller declares it, this
 * module allocates nothing.
 *
 * Timestamps are int64_t rather than a 32-bit millisecond counter on purpose.
 * k_uptime_get() is int64_t, and truncating it puts a wrap at 49.7 days inside
 * a security signal that is supposed to run for years.
 */
struct woz_slam_state {
	int64_t last_accept_ms;  /**< when the last strike was accepted */
	int64_t window_start_ms; /**< when the current burst window opened */
	uint8_t window_count;    /**< accepted strikes inside that window */
	bool tamper_latched;     /**< tamper reported and not yet cleared */
	bool seeded;             /**< false until the first strike; see woz_slam_poll */
};

/** Reset to "nothing has ever hit this door". */
void woz_slam_init(struct woz_slam_state *s);

/**
 * Fold one poll of the interrupt latch into the state.
 *
 * @param cfg        Tuning; NULL or a zero tamper_count yields WOZ_SLAM_NONE.
 * @param s          Caller-owned state, previously woz_slam_init()ed.
 * @param struck     True if the GPIO callback latched at least one edge since
 *                   the previous poll. The caller is expected to test-and-clear
 *                   its atomic, so a burst arriving between two polls is one
 *                   `true` and is deliberately counted once: the poll rate is
 *                   the debounce floor.
 * @param now_ms     Monotonic milliseconds, k_uptime_get() on target.
 * @return WOZ_SLAM_TAMPER on the poll that latches tamper, WOZ_SLAM_IMPACT on
 *         an accepted strike, WOZ_SLAM_NONE otherwise. TAMPER is returned once
 *         per latch, not once per subsequent strike, so a caller that logs it
 *         cannot be made to log forever by continuing to hit the door.
 */
enum woz_slam_event woz_slam_poll(const struct woz_slam_cfg *cfg, struct woz_slam_state *s,
				  bool struck, int64_t now_ms);

/** True while tamper is latched. Survives until woz_slam_clear_tamper(). */
bool woz_slam_tampered(const struct woz_slam_state *s);

/**
 * Clear the tamper latch. Deliberately explicit rather than time-based: a door
 * that was being attacked should stay flagged until something decides the
 * incident is over, and that decision does not belong to a 250 ms loop.
 */
void woz_slam_clear_tamper(struct woz_slam_state *s);

#endif /* WOZ_SLAM_H */
