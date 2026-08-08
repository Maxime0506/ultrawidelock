/**
 * @file matter_status.h — return codes shared by every woz_matter layer.
 */
/*
 * One code space for the whole node, so a value returned by the TLV codec and
 * one returned by the message layer never mean different things. MATTER_END is
 * positive on purpose: running out of elements is an outcome, not a failure,
 * and `while (next() == MATTER_OK)` should not have to know the difference.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum matter_status {
	/** No more elements at this level. Expected, not a failure. */
	MATTER_END = 1,
	MATTER_OK = 0,
	/** Ran out of output buffer. */
	MATTER_E_NOSPACE = -1,
	/** Caller passed, or the wire carried, something unrepresentable. */
	MATTER_E_INVAL = -2,
	/** Nesting past the depth cap. */
	MATTER_E_DEPTH = -3,
	/** Operation does not fit the current state. */
	MATTER_E_STATE = -4,
	/** Input ended mid-element, or a container never closed. */
	MATTER_E_TRUNC = -5,
	/** Accessor did not match the element actually present. */
	MATTER_E_TYPE = -6,
	/** Message counter already seen. Drop the payload, but still acknowledge. */
	MATTER_E_DUP = -7,
	/**
	 * Waited as long as the caller allowed and it did not happen.
	 *
	 * Distinct from MATTER_E_STATE because it is an ANSWER: a Thread attach
	 * that has not completed yet is a fact to report to the commissioner,
	 * not a malfunction to hide.
	 */
	MATTER_E_TIMEOUT = -8,
};

#ifdef __cplusplus
}
#endif
