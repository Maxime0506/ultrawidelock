/**
 * @file anchor_twr.h — anchor-to-anchor DS-TWR, both roles.
 *
 * A three-message double-sided two-way ranging round between two boards this
 * project owns both ends of. No CCC, no Aliro, no credential, no phone: the
 * whole thing runs at the CONFIG_ULTRAWIDELOCK_UWB tier, where uwb_seam.h inlines to the
 * bare decadriver and there is no STS engine to bind.
 *
 *   initiator                     responder
 *     t1  POLL  ---------------->  t2
 *     t4  <---------------  RESP   t3   (delayed TX, ANCHOR_REPLY_DELAY_US)
 *     t5  FINAL ---------------->  t6   (delayed TX, ANCHOR_REPLY_DELAY_US)
 *                carries t_round1 = t4-t1 and t_reply2 = t5-t4
 *
 * The responder holds t2, t3 and t6 itself, so the FINAL's two intervals
 * complete the set and it is the responder that computes the distance.
 *
 * Everything polls SYS_STATUS rather than taking DW3000 callbacks: the whole
 * exchange stays in thread context, so "nothing logs from a ranging callback"
 * holds by construction. uwb_min_twr_exchange() is the model.
 */

#ifndef ANCHOR_TWR_H
#define ANCHOR_TWR_H

#include <stdint.h>

/** Counters for the whole run; read with anchor_twr_stats(). */
struct anchor_twr_stats {
	uint32_t rounds;   /**< POLLs sent (initiator) or POLLs received (responder). */
	uint32_t ranges;   /**< Rounds that produced a distance (responder only). */
	uint32_t timeouts; /**< Rounds lost to an RX timeout at either step. */
	uint32_t late;     /**< Delayed TX refused by the chip: the arm missed its slot. */
	uint32_t rejected; /**< Ranges outside the plausibility band. */
	/** Worst |predicted t5 - actual t5| seen, DTU. Proves the prediction is a
	 *  constant the calibration absorbs; a drifting value means it is not. */
	uint32_t t5_err_max;
	/** Best |predicted t5 - actual t5| seen, DTU. The max alone cannot tell a
	 *  constant from a spread, and the spread is the question: 256 DTU of
	 *  reply-time error is 64 DTU of time-of-flight and therefore 300 mm,
	 *  against a measured 302 mm gap between the two range modes. Initialised
	 *  to UINT32_MAX in anchor_twr_init(), so max < min means no delayed TX
	 *  has completed yet rather than an impossible measurement. */
	uint32_t t5_err_min;
	/** Most recent PUBLISHED distance, mm: the median of the last
	 *  CONFIG_ANCHOR_MEDIAN_N raw samples, not the newest one. */
	int32_t last_mm;
	/** Most recent RAW distance, mm, before the median. Kept because the
	 *  half-chip leading-edge slip the median exists to reject is only
	 *  visible here -- if this stops being bimodal, the filter can go. */
	int32_t last_raw_mm;
	/** ipatovFpIndex from the FINAL reception that produced last_raw_mm,
	 *  Q10.6: the integer part is the accumulator tap, the low 6 bits are
	 *  sixty-fourths of one. One tap is 300.4 mm, and the measured gap
	 *  between the two range modes is 302 mm, so this is the register that
	 *  says whether those are the same phenomenon. */
	uint32_t last_fp_index;
	/** xtalOffset from the same reception: the measured clock offset between
	 *  the two boards. The first-path index ramps and wraps with period 8,
	 *  which is a clock phase relationship, and this is the register that says
	 *  whether the ramp rate matches the clock or is something else. */
	int32_t last_xtal_offset;
};

/** Bring the radio up and apply the anchor PHY (SP0, STS off). 0 on success. */
int anchor_twr_init(void);

/** Run one initiator round. Returns 0 when the FINAL was transmitted. */
int anchor_twr_initiator_round(uint32_t seq);

/**
 * Wait for one responder round.
 *
 * @param mm_out  accepted distance, mm (written only on 0 return).
 * @param seq_out round sequence the initiator stamped (written only on 0 return).
 * @return 0 on an accepted range, negative otherwise.
 */
int anchor_twr_responder_round(int32_t *mm_out, uint32_t *seq_out);

/** Copy the counters out. */
void anchor_twr_stats(struct anchor_twr_stats *out);

#endif /* ANCHOR_TWR_H */
