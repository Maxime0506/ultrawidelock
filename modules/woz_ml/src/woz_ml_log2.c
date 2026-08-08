/**
 * @file woz_ml_log2.c — log2 of an integer from its leading bit and a small table.
 *
 * log2(v) splits exactly into an integer part and a fractional one:
 *
 *     v = 2^e * (1 + m),  0 <= m < 1
 *     log2(v) = e + log2(1 + m)
 *
 * e is just where the leading one sits, which is one CLZ instruction on
 * Cortex-M4, and m is the bits underneath it. So the only thing that needs
 * approximating is log2(1 + m) over a single octave, and that is what the
 * generated table holds: nine entries, linearly interpolated.
 *
 * WHY NINE ENTRIES IS ENOUGH, measured rather than assumed. Linear
 * interpolation of log2(1+m) over eight segments has a worst-case error of
 * h^2/8 * max|f''| = 0.0029 in log2 units, at m near zero where the curve bends
 * hardest. Through this module's callers that is 0.0086 dB and 0.017 dB. The
 * error that actually limits the feature is neither of those: it is the reader
 * reporting range as a whole number of centimetres, worth ~0.4 dB at the short
 * end, which is why table sizes from 8 to 64 entries all bottom out at the same
 * 0.183 dB in gate 3. Adding entries buys nothing while that term stands.
 */

#include "woz_ml_log2.h"
#include "woz_ml_log2_table.h"

#if defined(__GNUC__) || defined(__clang__)
#define COUNT_LEADING_ZEROS_64(x) __builtin_clzll(x)
#else
/* One CLZ pair on Cortex-M4; this fallback exists so the file stays C99 rather
 * than GCC-only, and is never what the firmware compiles. */
static int count_leading_zeros_64(uint64_t v)
{
	int n = 0;

	while (!(v & 0x8000000000000000ull)) {
		v <<= 1;
		n++;
	}
	return n;
}
#define COUNT_LEADING_ZEROS_64(x) count_leading_zeros_64(x)
#endif

/* 2^-32, as a multiply so no float division appears anywhere in this module. */
#define TWO_POW_MINUS_32 2.3283064365386963e-10f

float woz_ml_log2_u64(uint64_t v)
{
	int lz, exponent;
	uint32_t mantissa, index;
	float weight;

	if (v == 0u) {
		return 0.0f;
	}

	lz = COUNT_LEADING_ZEROS_64(v);
	exponent = 63 - lz;

	/* The bits under the leading one, left-aligned into 32. Shifting a 64-bit
	 * value by 64 is undefined, and lz == 63 is exactly v == 1, whose mantissa
	 * is zero; hence the branch rather than a clever shift. */
	mantissa = (lz >= 63) ? 0u : (uint32_t)((v << (lz + 1)) >> 32);

	index = mantissa >> (32 - WOZ_ML_LOG2_TABLE_BITS);
	weight = (float)(mantissa << WOZ_ML_LOG2_TABLE_BITS) * TWO_POW_MINUS_32;

	return (float)exponent + woz_ml_log2_tab[index] +
	       weight * (woz_ml_log2_tab[index + 1] - woz_ml_log2_tab[index]);
}
