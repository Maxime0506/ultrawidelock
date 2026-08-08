<!-- generated documentation — edit the source, not this file -->
# `modules/woz_ml/src/woz_ml_log2.c`

@file woz_ml_log2.c — log2 of an integer from its leading bit and a small table.
log2(v) splits exactly into an integer part and a fractional one:
v = 2^e * (1 + m),  0 <= m < 1
log2(v) = e + log2(1 + m)
e is just where the leading one sits, which is one CLZ instruction on
Cortex-M4, and m is the bits underneath it. So the only thing that needs
approximating is log2(1 + m) over a single octave, and that is what the
generated table holds: nine entries, linearly interpolated.
WHY NINE ENTRIES IS ENOUGH, measured rather than assumed. Linear
interpolation of log2(1+m) over eight segments has a worst-case error of
h^2/8 * max|f''| = 0.0029 in log2 units, at m near zero where the curve bends
hardest. Through this module's callers that is 0.0086 dB and 0.017 dB. The
error that actually limits the feature is neither of those: it is the reader
reporting range as a whole number of centimetres, worth ~0.4 dB at the short
end, which is why table sizes from 8 to 64 entries all bottom out at the same
0.183 dB in gate 3. Adding entries buys nothing while that term stands.

**depends on** [`modules/woz_ml/src/woz_ml_log2.h`](woz_ml_log2.h.md), [`modules/woz_ml/src/woz_ml_log2_table.h`](woz_ml_log2_table.h.md)

## API

### `static int count_leading_zeros_64(uint64_t v)`
`modules/woz_ml/src/woz_ml_log2.c:32`

One CLZ pair on Cortex-M4; this fallback exists so the file stays C99 rather
than GCC-only, and is never what the firmware compiles.

### `float woz_ml_log2_u64(uint64_t v)`
`modules/woz_ml/src/woz_ml_log2.c:48`

log2(v), from the leading-bit position plus a table of log2(1 + x).
@param v  strictly positive. Zero returns 0.0f rather than diverging, because
a defined answer beats undefined behaviour on a register read that
failed; callers reject a zero before they get here, since a zeroed
CIA register is a failed read rather than a very weak channel.
64-bit and not 32-bit because woz_ml_feat.c needs log2 of a 17-bit channel
area shifted left by 17 and of a sum of three squares, neither of which fits.
The range correction shares it rather than keeping a second copy of the same
table.
Maximum error against float64 is 0.0029 in log2 units, which is 0.0086 dB
through a 10*log10 and 0.017 dB through a 20*log10. That is an order of
magnitude under the 0.183 dB the range correction already accepts from
centimetre-quantised input, so it is not the term that matters. See
woz_ml_log2.c.
