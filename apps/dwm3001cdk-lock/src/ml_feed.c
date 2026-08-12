/*
 * ml_feed.c — the channel-classifier glue between the CIA latch and the
 * approach controller. Split out of main.c so the grant loop reads as policy
 * and this file carries the measurement mechanics.
 */
#include "ml_feed.h"

#include "uwb_cirdiag.h" /* latched Ipatov scalars, for the channel classifier */

#if defined(CONFIG_ULTRAWIDELOCK_ML_LOS)
#include "ultrawidelock_ml.h"
#include "woz_log.h"  /* woz_printf -- the [ALAB] ev=ml classifier trace */
#include "woz_port.h" /* woz_uptime_us -- the [ALAB] timebase */
#endif

/**
 * Feed one trusted range, carrying this reception's channel class if there is one.
 *
 * WHERE THIS RUNS, because it is the only reason it is affordable. The classifier
 * needs dwt_readdiagnostics(), measured at 972 us on this board -- 53% of the
 * ~1836 us ranging arm deadline, which would be reckless on the RX path. It is
 * not on the RX path. uwb_cirdiag_capture() already takes that read AFTER the
 * shim re-arms, and this function only copies the result out in the main loop,
 * one ranging block (~192 ms) later. The work added here is five register copies,
 * three logarithms and two comparisons.
 *
 * The channel is read only when the capture counter has ADVANCED. The latch is
 * latest-wins with no queue, so a stale snapshot re-read across several ranging
 * rounds would let one obstructed reception carry a whole median window and
 * defeat the majority-of-five that gates the widening.
 *
 * Falls back to the plain feed whenever anything is missing -- stream disarmed,
 * nothing captured, a failed CIA read, or the classifier compiled out. A missing
 * class must read as CLEAR rather than as obstructed: clear is the unwidened
 * threshold, which is the behaviour that shipped.
 */
enum ultrawidelock_approach_action ml_feed_range(struct ultrawidelock_approach *ap, int64_t now,
					 int32_t cm)
{
#if defined(CONFIG_ULTRAWIDELOCK_ML_LOS) && defined(CONFIG_ULTRAWIDELOCK_UWB_CIRDIAG)
	static uint32_t last_diag_n;
	struct uwb_cirdiag_ipatov ip;

	if (cm >= 0 && uwb_cirdiag_last_ipatov(&ip) && ip.n != last_diag_n) {
		const struct ultrawidelock_ml_cia cia = {
			.f1 = ip.f1,
			.f2 = ip.f2,
			.f3 = ip.f3,
			.accum_count = ip.accum_count,
			.channel_area = ip.power,
		};
		float feat[ULTRAWIDELOCK_ML_LOS_N_FEATURES];
		float pwr_diff;

		last_diag_n = ip.n;
		if (ultrawidelock_ml_los_features(&cia, (uint16_t)cm, feat, &pwr_diff)) {
			const enum ultrawidelock_ml_los_class cls = ultrawidelock_ml_los_classify(feat);
			const float conf = ultrawidelock_ml_los_confidence(feat);

			/*
			 * One line per fresh latch, joinable to its ev=uwb.diag line by
			 * n=. conf_c is the dB-scaled confidence in centi-units, so the
			 * 2.61 vote gate reads as 261; dis is the tree-vs-vendor
			 * disagreement whose RATE is the label-free drift monitor
			 * ultrawidelock_ml_los_vendor() documents. Main-loop context, one ranging
			 * block after the reception, so this competes with no deadline.
			 */
			woz_printf("[ALAB] t=%lld ev=ml n=%u cm=%d cls=%u conf_c=%d dis=%u\n",
				   woz_uptime_us(), ip.n, cm, (unsigned)cls,
				   (int)(conf * 100.0f),
				   (unsigned)ultrawidelock_ml_los_disagrees(feat, pwr_diff));
			return ultrawidelock_approach_feed_channel(ap, now, cm,
							   cls == ULTRAWIDELOCK_ML_LOS_OBSTRUCTED, conf);
		}
	}
#endif
	return ultrawidelock_approach_feed(ap, now, cm);
}

/**
 * The debounced verdict, printed on the edge only. This is the state
 * the widening consumes, so a walk with nlos_widen_cm still 0 shows
 * exactly where a widened build would have moved its threshold --
 * which is the reading that chooses the number.
 */
void ml_feed_vote_trace(struct ultrawidelock_approach *ap, int64_t now)
{
#if defined(CONFIG_ULTRAWIDELOCK_ML_LOS) && defined(CONFIG_ULTRAWIDELOCK_UWB_CIRDIAG)
	static bool was_blocked;
	const bool blocked = ultrawidelock_approach_nlos_blocked(ap, now);

	if (blocked != was_blocked) {
		was_blocked = blocked;
		woz_printf("[ALAB] t=%lld ev=ml.vote blocked=%u\n",
			   woz_uptime_us(), (unsigned)blocked);
	}
#else
	(void)ap;
	(void)now;
#endif
}
