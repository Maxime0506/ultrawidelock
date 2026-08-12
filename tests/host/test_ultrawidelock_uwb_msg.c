/** @file test_ultrawidelock_uwb_msg.c — M1-M4 builders + ranging/notification dispatch. */
#include <stdlib.h>
#include <string.h>

#include <deca_device_api.h> /* ultrawidelock_host_rx radio knobs */

#include "ultrawidelock_uwb_internal.h"
#include "ultrawidelock_uwb_msg.h"
#include "ultrawidelock_uwb_msg_builder.h"
#include "ultrawidelock_uwb_msg_parser.h"
#include "ultrawidelock_uwb_msg_spec.h"
#include "test.h"

/* --- transmit + event capture ------------------------------------------- */

static struct {
	struct ultrawidelock_uwb_message *msg;
	int count;
} g_tx;

static void tx_cb(struct ultrawidelock_uwb_message *m, struct ultrawidelock_uwb_session *s,
		  void *user_data, bool timeout)
{
	(void)s;
	(void)user_data;
	(void)timeout;
	if (g_tx.msg) {
		ultrawidelock_uwb_msg_free(g_tx.msg);
	}
	g_tx.msg = m;
	g_tx.count++;
}

static void ev_cb(struct ultrawidelock_uwb_session_event *e, void *user_data)
{
	(void)user_data;
	ultrawidelock_uwb_session_event_free(e);
}

/* --- message helpers ---------------------------------------------------- */

static struct ultrawidelock_uwb_msg_builder mk(uint8_t proto, uint8_t id)
{
	struct ultrawidelock_uwb_msg_builder b;

	ultrawidelock_uwb_msg_builder_init(&b, 128u);
	ultrawidelock_uwb_msg_builder_header(&b, proto, id, 0u);
	return b;
}

static void fix_plen(struct ultrawidelock_uwb_message *m)
{
	uint16_t plen = (uint16_t)(m->len - ULTRAWIDELOCK_HEADER_LENGTH);

	m->data[2] = (uint8_t)(plen >> 8);
	m->data[3] = (uint8_t)plen;
}

/* Append an attribute of a deliberately wrong width to force a read failure. */
static void add_wrong(struct ultrawidelock_uwb_msg_builder *b, uint8_t id, int width)
{
	if (width == 1) {
		ultrawidelock_uwb_msg_builder_add_u8(b, id, 0u);
	} else {
		ultrawidelock_uwb_msg_builder_add_u16(b, id, 0u);
	}
}

static void add_m2_attrs(struct ultrawidelock_uwb_msg_builder *b)
{
	ultrawidelock_uwb_msg_builder_add_u16(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 0x0001u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO, 0x00u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x01u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER, 4u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, 0x01u);
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK, 0x05u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
}

static void add_m4_attrs(struct ultrawidelock_uwb_msg_builder *b)
{
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x1000u);
	ultrawidelock_uwb_msg_builder_add_u64(b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	ultrawidelock_uwb_msg_builder_add_u32(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, 0x11223344u);
	ultrawidelock_uwb_msg_builder_add_u8(
		b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX, 9u);
}

/* Read the u32 value of the first attribute with @id, or fail_val if absent. */
static uint32_t find_u32(struct ultrawidelock_uwb_message *m, uint8_t id, uint32_t fail_val)
{
	struct ultrawidelock_uwb_msg_parser p = ULTRAWIDELOCK_UWB_MSG_PARSER_INIT(m);
	struct ultrawidelock_uwb_msg_attribute *a;
	uint32_t v;

	while ((a = ultrawidelock_uwb_msg_next_attribute(&p))) {
		if (a->id == id && ultrawidelock_uwb_msg_read_u32(a, "x", &v)) {
			return v;
		}
	}
	return fail_val;
}

void test_ultrawidelock_uwb_msg(void)
{
	const uint32_t SID = 0x11223344u;
	uint16_t pvs[1] = { 0x0100u };
	uint16_t cfgs[1] = { 0x0001u };
	uint8_t combos[1] = { 0x00u };
	uint8_t oui[3] = { 0xa1, 0xb2, 0xc3 };
	uint8_t ursk[32];
	struct cherry_ccc_capabilities ccc;
	struct cherry_core_event_device_capabilities caps;
	struct ultrawidelock_uwb_adapter_reader_config cfg;
	struct ultrawidelock_uwb_msg_builder b;
	struct ultrawidelock_uwb_message *m;

	memset(&g_tx, 0, sizeof(g_tx));
	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}

	memset(&ccc, 0, sizeof(ccc));
	ccc.protocol_versions.len = 1u;
	ccc.protocol_versions.items = pvs;
	ccc.uwb_configs.len = 1u;
	ccc.uwb_configs.items = cfgs;
	ccc.pulse_shape_combos.len = 1u;
	ccc.pulse_shape_combos.items = combos;
	ccc.minimum_ran_multiplier = 4u;
	ccc.slot_bitmask = 0x01u;
	ccc.channel_bitmask = 0x01u;
	ccc.hopping_config_bitmask = 0x0Au;
	ccc.sync_code_index_bitmask = 0x00000005u;
	memset(&caps, 0, sizeof(caps));
	caps.ccc_capabilities = &ccc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.min_ran_multiplier = 8u;
	cfg.preferred_hopping_configs.count = 1u;
	cfg.preferred_hopping_configs.configs[0] =
		ULTRAWIDELOCK_HOPPING_CONFIG_CONTINUOUS_DEFAULT;

	struct cherry *cx = cherry_create("host", NULL, NULL);
	struct ultrawidelock_uwb_adapter *adapter =
		ultrawidelock_uwb_adapter_create_reader(cx, &caps, &cfg);
	struct ultrawidelock_uwb_session *s =
		ultrawidelock_uwb_session_create(adapter, SID, ev_cb, tx_cb, NULL);
	T_OK("setup", adapter != NULL && s != NULL);
	ultrawidelock_uwb_session_set_ursk(s, ursk);
	ultrawidelock_uwb_session_set_protocol_version(s, 0x0100u);

	t_group("build M1 (header + attributes round-trip)");
	m = ultrawidelock_uwb_msg_build_m1(s);
	T_OK("m1", m != NULL);
	T_EQ("m1.proto", ultrawidelock_uwb_msg_protocol_header(m->data),
	     ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE);
	T_EQ("m1.id", ultrawidelock_uwb_msg_message_id(m->data), ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M1);
	T_EQ("m1.plen", ultrawidelock_uwb_msg_payload_length(m->data),
	     (uint16_t)(m->len - ULTRAWIDELOCK_HEADER_LENGTH));
	T_EQ("m1.sid",
	     find_u32(m, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 0u), SID);
	ultrawidelock_uwb_msg_free(m);

	t_group("build suspend/resume requests + general error");
	m = ultrawidelock_uwb_msg_build_suspend_resume_request(s, true);
	T_EQ("suspreq.id", ultrawidelock_uwb_msg_message_id(m->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);
	T_EQ("suspreq.sid",
	     find_u32(m, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 0u), SID);
	ultrawidelock_uwb_msg_free(m);
	m = ultrawidelock_uwb_msg_build_suspend_resume_request(s, false);
	T_EQ("resreq.id", ultrawidelock_uwb_msg_message_id(m->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_RESUME_REQUEST);
	ultrawidelock_uwb_msg_free(m);
	m = ultrawidelock_uwb_msg_build_general_error(
		s, ULTRAWIDELOCK_UWB_NOTIFICATION_GENERAL_ERROR_UNKNOWN);
	T_EQ("gerr.proto", ultrawidelock_uwb_msg_protocol_header(m->data),
	     ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION);
	T_EQ("gerr.id", ultrawidelock_uwb_msg_message_id(m->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT);
	ultrawidelock_uwb_msg_free(m);

	t_group("process_ranging guards + unsupported id");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_REQUEST);
	fix_plen(b.message);
	T_EQ("pr.null.s", ultrawidelock_uwb_msg_process_ranging(NULL, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pr.null.m", ultrawidelock_uwb_msg_process_ranging(s, NULL),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pr.unsup", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MESSAGE_UNSUPPORTED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("full handshake M1 -> M2 -> M4 (session goes RANGING)");
	T_EQ("init", ultrawidelock_uwb_session_init_setup(s), ULTRAWIDELOCK_UWB_ERR_NONE);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2", ultrawidelock_uwb_msg_process_ranging(s, b.message), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("m2.state", s->state, M3_SENT);
	T_EQ("m3.id", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M3);
	ultrawidelock_uwb_msg_free(b.message);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4", ultrawidelock_uwb_msg_process_ranging(s, b.message), ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("m4.state", s->state, RANGING);
	T_OK("m4.ccc", s->ccc_session != NULL);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("M2 vendor variant (mask + VENDOR_SPECIFIC attribute)");
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	ultrawidelock_uwb_msg_builder_add_bytes(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_VENDOR_SPECIFIC, 3u, oui);
	fix_plen(b.message);
	T_EQ("m2.vendor", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("m2v.state", s->state, M3_SENT);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("ranging error paths");
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u16(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 1u);
	fix_plen(b.message);
	T_EQ("m2.missing", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x04u);
	fix_plen(b.message);
	T_EQ("m2.badchan", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0x00u);
	fix_plen(b.message);
	T_EQ("m2.nohop", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = RANGING; /* wrong state for M2 */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2.badstate", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = M1_SENT; /* wrong state for M4 */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4.badstate", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = M3_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x1000u);
	fix_plen(b.message);
	T_EQ("m4.missing", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("every attribute parser rejects a wrong-width value");
	{
		struct {
			uint8_t id;
			int width;
		} bad[] = {
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 1 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK, 1 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 1 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0, 1 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, 1 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STATUS, 2 },
			{ ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 1 },
		};

		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
			s->state = M1_SENT;
			b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
			       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
			add_wrong(&b, bad[i].id, bad[i].width);
			fix_plen(b.message);
			T_EQ("attr.badwidth",
			     ultrawidelock_uwb_msg_process_ranging(s, b.message),
			     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
			ultrawidelock_uwb_msg_free(b.message);
		}
	}

	t_group("unknown attribute id is ignored (parse continues)");
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT, 0u);
	fix_plen(b.message);
	/* Parsed (default case), then fails the M2 attribute-mask check. */
	T_EQ("m2.unknownattr", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("suspend request handler (RANGING) transmits a response");
	s->state = RANGING;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID);
	fix_plen(b.message);
	T_EQ("susp.req", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("susp.resp.id", ultrawidelock_uwb_msg_message_id(g_tx.msg->data),
	     ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("suspend response reject keeps ranging");
	s->state = SUSPEND_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u8(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ULTRAWIDELOCK_UWB_RANGING_SERVICE_STATUS_REJECT);
	fix_plen(b.message);
	T_EQ("susp.rej", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("susp.rej.state", s->state, RANGING);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("suspend response accept stops");
	s->state = SUSPEND_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u8(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ULTRAWIDELOCK_UWB_RANGING_SERVICE_STATUS_ACCEPT);
	fix_plen(b.message);
	T_EQ("susp.acc", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("susp.acc.state", s->state, SUSPENDED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("resume response restarts ranging (time offset applied)");
	ultrawidelock_uwb_session_set_time_offset(s, 1000);
	s->state = RESUME_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
	ultrawidelock_uwb_msg_builder_add_u64(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	fix_plen(b.message);
	T_EQ("resume.resp", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	T_EQ("resume.resp.state", s->state, RANGING);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("event notification (busy / general error / descriptor / unknown)");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT);
	ultrawidelock_uwb_msg_builder_add_bytes(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_BUSY, 0u, NULL);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_GENERAL_ERROR, 2u);
	ultrawidelock_uwb_msg_builder_add_bytes(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_READER_DESCRIPTOR, 0u,
		NULL);
	ultrawidelock_uwb_msg_builder_add_bytes(&b, 0x7Fu, 0u, NULL);
	fix_plen(b.message);
	T_EQ("notif.event", ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT);
	ultrawidelock_uwb_msg_builder_add_u16(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_GENERAL_ERROR, 0u);
	fix_plen(b.message);
	T_EQ("notif.event.bad",
	     ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("ranging notification drives the state machine");
	struct {
		uint8_t attr;
		enum ultrawidelock_uwb_session_state pre;
		enum ultrawidelock_uwb_session_state post;
	} rn[] = {
		{ ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING, CREATED,
		  M1_SENT },
		{ ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_SETUP_LATER,
		  M1_SENT, CREATED },
		{ ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_RANGING_SUSPENDED,
		  RANGING, SUSPENDED },
		{ ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_RESUME,
		  SUSPENDED, RESUME_REQ_SENT },
		{ ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_RESUME_LATER,
		  RESUME_REQ_SENT, SUSPENDED },
	};
	for (size_t i = 0; i < sizeof(rn) / sizeof(rn[0]); i++) {
		s->state = rn[i].pre;
		b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
		       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
		ultrawidelock_uwb_msg_builder_add_u8(&b, rn[i].attr, 0u);
		fix_plen(b.message);
		T_EQ("rn.ok", ultrawidelock_uwb_msg_process_notification(s, b.message),
		     ULTRAWIDELOCK_UWB_ERR_NONE);
		T_EQ("rn.state", s->state, rn[i].post);
		ultrawidelock_uwb_msg_free(b.message);
	}

	t_group("ranging notification: no-op attr + bad-state error");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_SECURE_RANGING_FAILED,
		0u);
	fix_plen(b.message);
	T_EQ("rn.secfail", ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	s->state = RANGING; /* wrong state for setup-later */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_SETUP_LATER,
		0u);
	fix_plen(b.message);
	T_EQ("rn.badstate", ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("informational + unknown notification ids");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_READER_STATUS_CHANGED);
	fix_plen(b.message);
	T_EQ("notif.readerstatus",
	     ultrawidelock_uwb_msg_process_notification(s, b.message), ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION, 0x7Fu);
	fix_plen(b.message);
	T_EQ("notif.unknownid",
	     ultrawidelock_uwb_msg_process_notification(s, b.message), ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("build M1 fails when a capability array is empty");
	s->ultrawidelock_ctx->ccc_caps.uwb_configs.len = 0u;
	T_OK("m1.nocfgs", ultrawidelock_uwb_msg_build_m1(s) == NULL);
	s->ultrawidelock_ctx->ccc_caps.uwb_configs.len = 1u;

	t_group("session-id mismatch rejects suspend request and M4");
	s->state = RANGING;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID ^ 1u);
	fix_plen(b.message);
	T_EQ("susp.badsid", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = M3_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID ^ 1u);
	fix_plen(b.message);
	T_EQ("m4.badsid", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("channel bitmask ch9 resolves to channel 9");
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x02u);
	fix_plen(b.message);
	/* Parsed (channel latched), then fails the M2 attribute-mask check. */
	T_EQ("m2.ch9", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	T_EQ("m2.ch9.chan", s->ccc_ultrawidelock_config.channel, 9);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("slot bitmask maps every chaps-per-slot bit");
	{
		struct {
			uint8_t bit;
			uint16_t slot_duration;
		} sb[] = {
			{ 0x02u, 400u * 4u },  { 0x04u, 400u * 6u },
			{ 0x08u, 400u * 8u },  { 0x10u, 400u * 9u },
			{ 0x20u, 400u * 12u }, { 0x40u, 400u * 24u },
			{ 0x80u, 400u * 0x80u }, /* no chaps bit: raw kept */
		};

		s->ultrawidelock_ctx->ccc_caps.slot_bitmask = 0xFFu;
		for (size_t i = 0; i < sizeof(sb) / sizeof(sb[0]); i++) {
			s->state = M1_SENT;
			b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
			       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
			ultrawidelock_uwb_msg_builder_add_u8(
				&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK,
				sb[i].bit);
			fix_plen(b.message);
			T_EQ("slot.parse", ultrawidelock_uwb_msg_process_ranging(s, b.message),
			     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
			T_EQ("slot.duration", s->ccc_ultrawidelock_config.slot_duration,
			     sb[i].slot_duration);
			ultrawidelock_uwb_msg_free(b.message);
		}
		s->ultrawidelock_ctx->ccc_caps.slot_bitmask = 0x01u;
	}

	t_group("hopping preference selection: disabled / adaptive / unknown");
	s->ultrawidelock_ctx->ccc_caps.hopping_config_bitmask = 0xFFu;
	cfg.preferred_hopping_configs.configs[0] = ULTRAWIDELOCK_HOPPING_CONFIG_DISABLED;
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
	fix_plen(b.message);
	T_EQ("hop.disabled", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED); /* mask check, after the hop parse */
	T_EQ("hop.disabled.mode", s->ccc_ultrawidelock_config.hopping_mode,
	     (enum cherry_ccc_hopping_mode)ULTRAWIDELOCK_HOPPING_CONFIG_DISABLED);
	ultrawidelock_uwb_msg_free(b.message);
	cfg.preferred_hopping_configs.configs[0] =
		ULTRAWIDELOCK_HOPPING_CONFIG_ADAPTIVE_DEFAULT;
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
	fix_plen(b.message);
	T_EQ("hop.adaptive", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	T_EQ("hop.adaptive.mode", s->ccc_ultrawidelock_config.hopping_mode,
	     (enum cherry_ccc_hopping_mode)ULTRAWIDELOCK_HOPPING_CONFIG_ADAPTIVE_DEFAULT);
	ultrawidelock_uwb_msg_free(b.message);
	cfg.preferred_hopping_configs.configs[0] = (enum ultrawidelock_hopping_config)0x55;
	s->state = M1_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
	fix_plen(b.message);
	T_EQ("hop.unknown", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED); /* unknown pref -> no common config */
	ultrawidelock_uwb_msg_free(b.message);
	cfg.preferred_hopping_configs.configs[0] =
		ULTRAWIDELOCK_HOPPING_CONFIG_CONTINUOUS_DEFAULT;
	s->ultrawidelock_ctx->ccc_caps.hopping_config_bitmask = 0x0Au;

	t_group("suspend request: missing attributes + wrong state");
	s->state = RANGING;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);
	ultrawidelock_uwb_msg_builder_add_u8(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ULTRAWIDELOCK_UWB_RANGING_SERVICE_STATUS_ACCEPT);
	fix_plen(b.message);
	T_EQ("susp.req.missing", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = CREATED;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_REQUEST);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID);
	fix_plen(b.message);
	T_EQ("susp.req.badstate", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("suspend response: parse error, missing attrs, wrong state");
	s->state = SUSPEND_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID ^ 1u);
	fix_plen(b.message);
	T_EQ("susp.resp.badsid", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = SUSPEND_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	fix_plen(b.message);
	T_EQ("susp.resp.missing", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = RANGING; /* wrong state for a suspend response */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_SUSPEND_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u8(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ULTRAWIDELOCK_UWB_RANGING_SERVICE_STATUS_ACCEPT);
	fix_plen(b.message);
	T_EQ("susp.resp.badstate", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("resume response: parse error, missing attrs, wrong state");
	s->state = RESUME_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID ^ 1u);
	fix_plen(b.message);
	T_EQ("resume.badsid", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = RESUME_REQ_SENT;
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
	fix_plen(b.message);
	T_EQ("resume.missing", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = RANGING; /* wrong state for a resume response */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
	ultrawidelock_uwb_msg_builder_add_u64(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	fix_plen(b.message);
	T_EQ("resume.badstate", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("ranging notification: bad-state resume-later + suspended");
	s->state = RANGING; /* wrong state for resume-later */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b,
		ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_RESUME_LATER,
		0u);
	fix_plen(b.message);
	T_EQ("rn.resumelater.badstate",
	     ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);
	s->state = CREATED; /* wrong state for ranging-suspended */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(
		&b, ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_RANGING_SUSPENDED,
		0u);
	fix_plen(b.message);
	T_EQ("rn.suspended.badstate",
	     ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_INVALID_STATE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("ranging notification: unknown attribute is logged, not fatal");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ULTRAWIDELOCK_UWB_MESSAGE_NOTIFICATION_RANGING);
	ultrawidelock_uwb_msg_builder_add_u8(&b, 0x7Fu, 0u);
	fix_plen(b.message);
	T_EQ("rn.unknownattr", ultrawidelock_uwb_msg_process_notification(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("supplementary-service messages are logged and accepted");
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_SUPPLEMENTARY_SERVICE, 0x01u);
	ultrawidelock_uwb_msg_builder_add_u8(&b, 0x10u, 0xAAu);
	fix_plen(b.message);
	T_EQ("suppl.ok", ultrawidelock_uwb_msg_process_supplementary(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_NONE);
	ultrawidelock_uwb_msg_free(b.message);

	t_group("resume response: ccc setter failure surfaces as internal");
	{
		struct cherry_ccc_session *saved = s->ccc_session;

		s->ccc_session = NULL; /* set_resume_params' ccc calls now reject */
		s->state = RESUME_REQ_SENT;
		b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
		       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
		ultrawidelock_uwb_msg_builder_add_u32(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
		ultrawidelock_uwb_msg_builder_add_u64(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0, 0u);
		fix_plen(b.message);
		T_EQ("resume.paramfail", ultrawidelock_uwb_msg_process_ranging(s, b.message),
		     ULTRAWIDELOCK_UWB_ERR_INTERNAL);
		ultrawidelock_uwb_msg_free(b.message);
		s->ccc_session = saved;
	}

	t_group("resume response: failed restart tears the session down");
	s->state = RESUME_REQ_SENT;
	ultrawidelock_host_rx.radio_init_ret = -5; /* the UWB re-listen must fail */
	b = mk(ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ULTRAWIDELOCK_UWB_MESSAGE_RESUME_RESPONSE);
	ultrawidelock_uwb_msg_builder_add_u32(
		&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
	ultrawidelock_uwb_msg_builder_add_u64(&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	fix_plen(b.message);
	T_EQ("resume.startfail", ultrawidelock_uwb_msg_process_ranging(s, b.message),
	     ULTRAWIDELOCK_UWB_ERR_SESSION_INIT);
	ultrawidelock_uwb_msg_free(b.message);
	ultrawidelock_host_rx.radio_init_ret = 0;
	/* The close-on-error branch already freed s; only the adapter remains. */

	if (g_tx.msg) {
		ultrawidelock_uwb_msg_free(g_tx.msg);
		g_tx.msg = NULL;
	}
	/* No session destroy here: the ranging flow above already tore it down
	 * via the CCC DEINIT event (which frees the session and its URSK). */
	ultrawidelock_uwb_adapter_destroy(adapter);
	cherry_destroy_sync(cx);
}
