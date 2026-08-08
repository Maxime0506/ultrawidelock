/**
 * @file woz_ml_feat.c — five CIA registers and a range, turned into the model's input.
 *
 * This is the half of the classifier that was missing: woz_ml_los_classify()
 * has always taken features in physical units, and until now nothing computed
 * them. ai/tinyml/parse_alab.py is the definition, and it is reproduced here
 * register for register rather than approximated, because the model was fitted
 * through exactly this arithmetic.
 *
 *     num    = F1^2 + F2^2 + F3^2
 *     fp_pwr = 10*log10(num / C^2)          - A
 *     rx_pwr = 10*log10(area * 2^17 / C^2)  - A
 *
 * where C is ipatovAccumCount and area is ipatovPower, the DW3000's 17-bit
 * "channel area". The 2^17 undoes that scaling, which is the one step in the
 * DW3000 formula that differs from the DW1000's and the one most likely to be
 * dropped by someone porting this from a DW1000 note.
 *
 * A IS NOT A CALIBRATION AND ITS VALUE DOES NOT MATTER HERE. It is eWINE's
 * PRF-64 constant, kept only so this arithmetic matches extract_features.py
 * exactly. It is a constant offset applied to both powers, so it cancels out of
 * pwr_diff entirely and shifts the tree's thresholds by a fixed amount that the
 * training already absorbed. Changing it does not recalibrate anything; it
 * invalidates the model.
 *
 * ZERO IS A FAILED READ, NOT A WEAK SIGNAL. A zeroed accumulator count or
 * channel area comes back from a CIA read that did not complete, and this is
 * not hypothetical: the CPER-set receptions in a real capture report
 * ipatovPower = 0. Left in, the logarithm turns them into -120 dB outliers that
 * dominate the mean and inflate the spread by 20 dB. parse_alab.py drops those
 * receptions and so does this, by returning false.
 */

#include <stddef.h>

#include "woz_ml.h"
#include "woz_ml_log2.h"

#if WOZ_ML_LOS_N_FEATURES != 2
#error "woz_ml_feat.c fills fp_resid and rx_pwr by name, and the generated \
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
	return K_DB_PER_LOG2_10 * (woz_ml_log2_u64(numerator) - 2.0f * woz_ml_log2_u64(count)) -
	       A_CONST_PRF64;
}

bool woz_ml_los_features(const struct woz_ml_cia *cia, uint16_t dist_cm,
			 float feat[WOZ_ML_LOS_N_FEATURES], float *pwr_diff_db)
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
	feat[WOZ_ML_LOS_F_FP_RESID] = woz_ml_los_fp_resid(fp_pwr, dist_cm);
	feat[WOZ_ML_LOS_F_RX_PWR] = rx_pwr;

	if (pwr_diff_db != NULL) {
		*pwr_diff_db = rx_pwr - fp_pwr;
	}

	return true;
}
