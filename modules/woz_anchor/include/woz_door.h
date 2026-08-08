/**
 * @file woz_door.h — door swing angle from a frame-to-leaf distance, and the
 *       closed/ajar/open state machine over it.
 *
 * One anchor on the frame, one on the leaf; law of cosines about the hinge:
 *
 *     d(theta)^2 = a^2 + b^2 - 2ab*cos(theta + theta0)
 *
 * Integer throughout (no FPU on the nRF52833). MOUNTING REQUIREMENT: a and b
 * must be approximately equal -- at 30 mm jitter, a = b = 850 mm resolves
 * 2.0 deg at shut, while a 200 mm mismatch collapses that to 35.7 deg,
 * destroying exactly the closed-vs-ajar call this exists to make while still
 * looking fine at wide angles. Check woz_door_resolution_mddeg(), not the tape
 * measure. Inherently coarse near fully open; nothing depends on that range.
 */

#ifndef WOZ_DOOR_H
#define WOZ_DOOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Angles are in millidegrees: integer, and fine enough that the jitter, not
 *  the unit, is the limit. 90 degrees is 90000. */
#define WOZ_DOOR_ANGLE_INVALID INT32_MIN

/** Fixed geometry, measured once at install. See the mounting requirement above. */
struct woz_door_cfg {
	int32_t hinge_to_frame_mm; /**< a: hinge axis to the frame anchor. */
	int32_t hinge_to_leaf_mm;  /**< b: hinge axis to the leaf anchor. */
	/**
	 * theta0 in millidegrees: the law-of-cosines angle when the door is
	 * SHUT, so a reported angle of 0 means shut.
	 *
	 * Solve it from three measured points (shut, ~45, ~90 degrees) rather
	 * than measuring to the hinge pin with a tape: the hinge axis is not
	 * where it looks like it is, and an error here biases every angle.
	 */
	int32_t offset_mddeg;
};

/** Door state. UNKNOWN until the first dwell completes, and after a bad read. */
enum woz_door_state {
	WOZ_DOOR_UNKNOWN = 0,
	WOZ_DOOR_CLOSED,
	WOZ_DOOR_AJAR,
	WOZ_DOOR_OPEN,
};

/**
 * Hysteresis band, in millidegrees, plus the dwell.
 *
 * Expressed in ANGLE rather than distance on purpose: re-hanging the door or
 * moving an anchor changes the distance that corresponds to "ajar" but not the
 * angle, so a geometry change is absorbed by recalibrating woz_door_cfg and
 * these stay put.
 */
struct woz_door_thresholds {
	int32_t closed_enter_mddeg; /**< below this, heading for CLOSED. */
	int32_t closed_leave_mddeg; /**< above this, no longer CLOSED. */
	int32_t open_enter_mddeg;   /**< above this, heading for OPEN. */
	int32_t open_leave_mddeg;   /**< below this, no longer OPEN. */
	uint8_t dwell;              /**< consecutive agreeing samples to commit. */
};

/** Factory defaults: 2/5 degrees closed, 35/30 degrees open, dwell 3. */
void woz_door_defaults(struct woz_door_thresholds *th);

/** Running state. Caller-owned; this module instantiates nothing. */
struct woz_door {
	struct woz_door_cfg cfg;
	struct woz_door_thresholds th;
	enum woz_door_state state;
	enum woz_door_state cand; /**< state the recent samples are arguing for. */
	uint8_t cand_n;           /**< how many in a row have argued for it. */
	int32_t last_mddeg;       /**< most recent angle, or WOZ_DOOR_ANGLE_INVALID. */
};

/** Initialise; @p th NULL takes woz_door_defaults(). Returns false if @p cfg is unusable. */
bool woz_door_init(struct woz_door *d, const struct woz_door_cfg *cfg,
		   const struct woz_door_thresholds *th);

/**
 * @brief Swing angle for a measured frame-to-leaf distance.
 *
 * @return Millidegrees from shut, or WOZ_DOOR_ANGLE_INVALID when @p d_mm is
 *         outside the range the geometry can produce ([|a-b|, a+b]) by more
 *         than the slack the law of cosines allows. A distance the hinge
 *         cannot explain is a measurement fault, not a very open door.
 */
int32_t woz_door_angle_mddeg(const struct woz_door_cfg *cfg, int32_t d_mm);

/** Feed one distance; returns the state after this sample. */
enum woz_door_state woz_door_feed(struct woz_door *d, int32_t d_mm);

/**
 * @brief Angle resolution at @p at_mddeg for a given distance jitter.
 *
 * The inverse of the sensitivity dd/dtheta = a*b*sin(theta+theta0)/d, so it
 * answers the only question that decides whether a mounting is good enough:
 * how many degrees of angle does one jitter-width of distance buy here. Use it
 * at the closed threshold, which is where it is worst and where it matters.
 *
 * @return Millidegrees per @p jitter_mm, or INT32_MAX where the geometry is
 *         blind (sensitivity zero).
 */
int32_t woz_door_resolution_mddeg(const struct woz_door_cfg *cfg, int32_t at_mddeg,
				  int32_t jitter_mm);

#endif /* WOZ_DOOR_H */
