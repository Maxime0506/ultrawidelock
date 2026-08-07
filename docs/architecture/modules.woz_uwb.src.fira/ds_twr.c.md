<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/fira/ds_twr.c`

*No module docstring. First commit: "woz_uwb: one signed DS-TWR estimator, shared by both callers".*

**depends on** [`modules/woz_uwb/src/fira/ds_twr.h`](ds_twr.h.md)  ·  **discussed in** [`anchor/README.md`](../../../anchor/README.md)

## API

### `int32_t ds_twr_tof_signed(const struct ds_twr *t)`
`modules/woz_uwb/src/fira/ds_twr.c:10`

@brief One-way time of flight, in ranging-timestamp ticks.
The asymmetric four-term estimator, which is what makes DS-TWR tolerant of
the two ends' clocks running at slightly different rates:
tof = (round1 * round2 - reply1 * reply2)
/ (round1 + round2 + reply1 + reply2)
@param t The four intervals; NULL yields 0.
@return Time of flight in ticks. NEGATIVE near contact, where measurement
noise exceeds the true flight time -- callers converting to a
distance must keep the sign and apply their own plausibility floor
rather than clamping here, because "slightly negative" and "wildly
wrong" want different responses. 0 if the denominator is 0.
