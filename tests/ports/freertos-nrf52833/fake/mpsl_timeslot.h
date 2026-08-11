/*
 * Model of the MPSL timeslot session, matching the pinned nrfxlib
 * mpsl_timeslot.h surface the port uses.
 *
 * The model actually schedules: a request drives the signal callback the way
 * MPSL would, delivering BLOCKED as many times as the test asks for before it
 * grants a slot, then START, then following the action the callback returns
 * until the session goes idle. A driver that mishandles a blocked request, or
 * that forgets to ask for another slot when its work is unfinished, fails here
 * rather than leaving a half-erased page on the board.
 */
#ifndef TEST_MPSL_TIMESLOT_H
#define TEST_MPSL_TIMESLOT_H

#include <stdbool.h>
#include <stdint.h>

#define MPSL_TIMESLOT_CONTEXT_SIZE 96u
#define MPSL_TIMESLOT_EARLIEST_TIMEOUT_MAX_US (256000000UL - 1UL)
#define MPSL_TIMESLOT_LENGTH_MAX_US 100000UL

enum MPSL_TIMESLOT_SIGNAL {
	MPSL_TIMESLOT_SIGNAL_START = 0,
	MPSL_TIMESLOT_SIGNAL_BLOCKED = 5,
	MPSL_TIMESLOT_SIGNAL_CANCELLED = 6,
	MPSL_TIMESLOT_SIGNAL_SESSION_IDLE = 7,
	MPSL_TIMESLOT_SIGNAL_INVALID_RETURN = 8,
	MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED = 9,
};

enum MPSL_TIMESLOT_SIGNAL_ACTION {
	MPSL_TIMESLOT_SIGNAL_ACTION_NONE = 0,
	MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND = 1,
	MPSL_TIMESLOT_SIGNAL_ACTION_END = 2,
	MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST = 3,
};

enum MPSL_TIMESLOT_HFCLK_CFG {
	MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED = 0,
	MPSL_TIMESLOT_HFCLK_CFG_NO_GUARANTEE = 1,
};

enum MPSL_TIMESLOT_PRIORITY {
	MPSL_TIMESLOT_PRIORITY_HIGH = 0,
	MPSL_TIMESLOT_PRIORITY_NORMAL = 1,
};

enum MPSL_TIMESLOT_REQUEST_TYPE {
	MPSL_TIMESLOT_REQ_TYPE_EARLIEST = 0,
	MPSL_TIMESLOT_REQ_TYPE_NORMAL = 1,
};

typedef uint8_t mpsl_timeslot_session_id_t;

typedef struct {
	uint8_t hfclk;
	uint8_t priority;
	uint32_t length_us;
	uint32_t timeout_us;
} mpsl_timeslot_request_earliest_t;

typedef struct {
	uint8_t hfclk;
	uint8_t priority;
	uint32_t distance_us;
	uint32_t length_us;
} mpsl_timeslot_request_normal_t;

typedef struct {
	uint8_t request_type;
	union {
		mpsl_timeslot_request_earliest_t earliest;
		mpsl_timeslot_request_normal_t normal;
	} params;
} mpsl_timeslot_request_t;

typedef struct {
	uint8_t callback_action;
	union {
		struct {
			mpsl_timeslot_request_t *p_next;
		} request;
		struct {
			uint32_t length_us;
		} extend;
	} params;
} mpsl_timeslot_signal_return_param_t;

typedef mpsl_timeslot_signal_return_param_t *(*mpsl_timeslot_callback_t)(
	mpsl_timeslot_session_id_t session_id, uint32_t signal);

int32_t mpsl_timeslot_session_count_set(void *p_mem, uint8_t n_sessions);
int32_t mpsl_timeslot_session_open(mpsl_timeslot_callback_t callback,
				   mpsl_timeslot_session_id_t *p_session_id);
int32_t mpsl_timeslot_session_close(mpsl_timeslot_session_id_t session_id);
int32_t mpsl_timeslot_request(mpsl_timeslot_session_id_t session_id,
			      mpsl_timeslot_request_t const *p_request);

/* Test control. */
void fake_timeslot_reset(void);
/* Blocked signals to deliver before granting a slot. */
extern unsigned fake_timeslot_blocks_before_grant;
/* Make the next session_count_set, session_open, or request fail. */
extern bool fake_timeslot_count_set_fails;
extern bool fake_timeslot_open_fails;
extern bool fake_timeslot_request_fails;
/* Grant no slot at all, which is what a permanently busy radio looks like. */
extern bool fake_timeslot_never_grants;

extern unsigned fake_timeslot_grants;
extern unsigned fake_timeslot_requests;
extern unsigned fake_timeslot_opens;
extern unsigned fake_timeslot_closes;
extern unsigned fake_timeslot_count_sets;
/* The length asked for by the most recent request. */
extern uint32_t fake_timeslot_last_length_us;
extern uint8_t fake_timeslot_last_request_type;
extern uint8_t fake_timeslot_last_hfclk;
/* Set when a request arrived with a length the API would refuse. */
extern unsigned fake_timeslot_violations;

#endif /* TEST_MPSL_TIMESLOT_H */
