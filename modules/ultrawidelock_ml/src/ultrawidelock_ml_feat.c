/* SPDX-License-Identifier: ISC */

/**
 * @file ultrawidelock_ml_feat.c — five CIA registers and a range, turned into the model's input.
 *
 * parse_alab.py (tinyml repo) is the definition, reproduced register for
 * register because the model was fitted through exactly this arithmetic:
 *
 *     num    = F1^2 + F2^2 + F3^2
 *     fp_pwr = 10*log10(num / C^2)          - A
 *     rx_pwr = 10*log10(area * 2^17 / C^2)  - A
 *
 * C = ipatovAccumCount, area = ipatovPower (17-bit; the 2^17 undoes DW3000
 * scaling absent on the DW1000). A is eWINE's PRF-64 constant, not a
 * calibration: it cancels out of pwr_diff and the training absorbed it --
 * changing it invalidates the model. Zero C or area is a failed CIA read, not a
 * weak signal (real CPER-set receptions report ipatovPower = 0; through the log
 * they become -120 dB outliers), so those receptions return false.
 */

#include <stddef.h>

#include "ultrawidelock_ml.h"
#include "ultrawidelock_ml_log2.h"

#if ULTRAWIDELOCK_ML_LOS_N_FEATURES != 2
#error "ultrawidelock_ml_feat.c fills fp_resid and rx_pwr by name, and the generated \
feature set no longer has exactly those two. Regenerating the model with a \
different SUBSET changes what a caller must supply; update this file to match \
rather than letting it fill a wrong-length vector."
#endif

/* 10*log10(x) = (10 / log2(10)) * log2(x). */
#define K_DB_PER_LOG2_10 3.01029996f

/* eWINE's PRF-64 constant. See the file header for why its value is not a knob. */
#define A_CONST_PRF64 121.74f

/* ipatovPower is a channel area scaled by 2^17, not a power. */
#define CHANNEL_AREA_SHIFT 17

/*
 * 10*log10(numerator / count^2) - A, with the division done as a subtraction of
 * logarithms so no float division appears and the numerator never has to be
 * representable as a float in the first place. count is squared in the log
 * domain for the same reason: count^2 is fine in 32 bits, but keeping it here
 * means one code path and one rounding behaviour for both callers.
 */
static float pwr_db(uint64_t numerator, uint32_t count)
{
	return K_DB_PER_LOG2_10 * (ultrawidelock_ml_log2_u64(numerator) -
				   2.0f * ultrawidelock_ml_log2_u64(count)) -
	       A_CONST_PRF64;
}

bool ultrawidelock_ml_los_features(const struct ultrawidelock_ml_cia *cia, uint16_t dist_cm,
			 float feat[ULTRAWIDELOCK_ML_LOS_N_FEATURES], float *pwr_diff_db)
{
	uint64_t num;
	float fp_pwr, rx_pwr;

	/* 64-bit because the fields are 22-bit in the DW3000's CIA registers even
	 * though dwt_rxdiag_t declares them uint32_t, so three squares reach 2^44
	 * and the shifted channel area reaches 2^34. Both overflow 32 bits. */
	num = (uint64_t)cia->f1 * cia->f1 + (uint64_t)cia->f2 * cia->f2 +
	      (uint64_t)cia->f3 * cia->f3;

	if (num == 0u || cia->accum_count == 0u || cia->channel_area == 0u) {
		return false;
	}

	fp_pwr = pwr_db(num, cia->accum_count);
	rx_pwr = pwr_db((uint64_t)cia->channel_area << CHANNEL_AREA_SHIFT, cia->accum_count);

	/* By name and not by position: the enum is generated, and a regenerated
	 * model that reorders it must not silently reorder the meaning of these. */
	feat[ULTRAWIDELOCK_ML_LOS_F_FP_RESID] = ultrawidelock_ml_los_fp_resid(fp_pwr, dist_cm);
	feat[ULTRAWIDELOCK_ML_LOS_F_RX_PWR] = rx_pwr;

	if (pwr_diff_db != NULL) {
		*pwr_diff_db = rx_pwr - fp_pwr;
	}

	return true;
}
