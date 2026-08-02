/** @file test_aliro_device_uwb.c — device-side M1-M4 setup codec, looped back
 *  against the real reader session. The device parses the reader's M1/M3 and
 *  builds M2/M4; the reader session (aliro_uwb_msg.c) must accept those and walk
 *  M1_SENT -> M3_SENT -> RANGING. EC-free; the strong anchor is that the shipped
 *  reader consumes the device's messages and reaches RANGING.
 */
#include <stdlib.h>
#include <string.h>

#include "aliro_device_uwb.h"
#include "aliro_uwb_internal.h" /* session struct + states + create/handle/destroy */
#include "aliro_uwb_msg.h"      /* header accessors + free */
#include "aliro_uwb_msg_builder.h"
#include "aliro_uwb_msg_parser.h"
#include "aliro_uwb_msg_spec.h"
#include "test.h"

/* transmit + event capture (same shape as test_aliro_session.c) */
static struct {
	struct aliro_uwb_message *msg;
	int count;
} g_tx;

static void tx_cb(struct aliro_uwb_message *m, struct aliro_uwb_session *s, void *user_data,
		  bool timeout)
{
	(void)s;
	(void)user_data;
	(void)timeout;
	if (g_tx.msg) {
		aliro_uwb_session_message_free(g_tx.msg);
	}
	g_tx.msg = m;
	g_tx.count++;
}

static void ev_cb(struct aliro_uwb_session_event *e, void *user_data)
{
	(void)user_data;
	aliro_uwb_session_event_free(e);
}

void test_aliro_device_uwb(void)
{
	const uint32_t SID = 0x11223344u;
	uint16_t pvs[1] = {0x0100u};
	uint16_t cfgs[1] = {0x0001u};
	uint8_t combos[1] = {0x00u};
	uint8_t ursk[32];
	struct cherry_ccc_capabilities ccc;
	struct cherry_core_event_device_capabilities caps;
	struct aliro_uwb_adapter_reader_config cfg;

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
	cfg.preferred_hopping_configs.configs[0] = ALIRO_HOPPING_CONFIG_CONTINUOUS_DEFAULT;
	cfg.r1_antennas[0] = 1u;
	cfg.r1_antennas[1] = 2u;
	cfg.r2_antennas[0] = 3u;
	cfg.r2_antennas[1] = 4u;

	struct cherry *cx = cherry_create("host", NULL, NULL);
	struct aliro_uwb_adapter *adapter = aliro_uwb_adapter_create_reader(cx, &caps, &cfg);
	struct aliro_uwb_session *s = aliro_uwb_session_create(adapter, SID, ev_cb, tx_cb, NULL);

	T_OK("setup", cx != NULL && adapter != NULL && s != NULL);
	aliro_uwb_session_set_ursk(s, ursk);
	aliro_uwb_session_set_protocol_version(s, 0x0100u);

	t_group("reader emits M1; device parses it");
	T_EQ("init.m1", aliro_uwb_session_init_setup(s), ALIRO_UWB_ERR_NONE);
	T_OK("m1.captured", g_tx.msg != NULL);

	struct aliro_dev_uwb_m1 m1;

	T_EQ("m1.parse", aliro_dev_uwb_parse_m1(g_tx.msg->data, g_tx.msg->len, &m1), 0);
	T_EQ("m1.session_id", m1.session_id, SID);
	T_OK("m1.config", m1.config_count == 1u && m1.config_ids[0] == 0x0001u);
	T_EQ("m1.channel", m1.channel_bitmask, 0x01);

	t_group("device builds M2; reader accepts -> M3_SENT");
	struct aliro_dev_uwb_m2_params m2p;

	aliro_dev_uwb_select_m2(&m1, &m2p);

	struct aliro_uwb_message *m2 = aliro_dev_uwb_build_m2(&m2p);

	T_OK("m2.built", m2 != NULL);
	T_EQ("m2.accepted", aliro_uwb_session_message_handle(s, m2), ALIRO_UWB_ERR_NONE);
	T_EQ("m2.state", s->state, M3_SENT);
	T_EQ("m3.emitted", aliro_uwb_msg_message_id(g_tx.msg->data), ALIRO_UWB_MESSAGE_SETUP_M3);
	aliro_uwb_msg_free(m2);

	t_group("device parses M3");
	struct aliro_dev_uwb_m3 m3;

	T_EQ("m3.parse", aliro_dev_uwb_parse_m3(g_tx.msg->data, g_tx.msg->len, &m3), 0);
	T_EQ("m3.nresp", m3.num_responders, 1);   /* ALIRO_NUM_RESPONDERS */
	T_EQ("m3.slots", m3.slots_per_round, 12); /* ALIRO_SLOTS_PER_ROUND_DEFAULT */
	T_EQ("m3.syncmask", m3.sync_code_index_bitmask, 0x05);

	t_group("device builds M4; reader accepts -> RANGING");
	struct aliro_dev_uwb_m4_params m4p = {
		.sts_index0 = 0x1000u,
		.uwb_time0 = 0u,
		.hop_mode_key = 0x11223344u,
		.sync_code_index = 9u,
	};
	struct aliro_uwb_message *m4 = aliro_dev_uwb_build_m4(&m4p);

	T_OK("m4.built", m4 != NULL);
	T_EQ("m4.accepted", aliro_uwb_session_message_handle(s, m4), ALIRO_UWB_ERR_NONE);
	T_EQ("m4.state", s->state, RANGING);
	aliro_uwb_msg_free(m4);

	t_group("round-trip: built M2 re-parses to the same attributes");
	m2 = aliro_dev_uwb_build_m2(&m2p);
	{
		struct aliro_uwb_msg_parser p = {
			.length = m2->len,
			.offset = ALIRO_HEADER_LENGTH,
			.data = m2->data,
		};
		struct aliro_uwb_msg_attribute *a;
		uint16_t cid = 0;
		uint8_t ch = 0, ran = 0;

		while ((a = aliro_uwb_msg_next_attribute(&p)) != NULL) {
			if (a->id == ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER) {
				aliro_uwb_msg_read_u16(a, "cid", &cid);
			} else if (a->id == ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK) {
				aliro_uwb_msg_read_u8(a, "ch", &ch);
			} else if (a->id == ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER) {
				aliro_uwb_msg_read_u8(a, "ran", &ran);
			}
		}
		T_OK("m2.roundtrip", cid == m2p.config_id && ch == m2p.channel_bitmask &&
					     ran == m2p.ran_multiplier);
	}
	aliro_uwb_msg_free(m2);

	t_group("parse rejects malformed input");
	struct aliro_dev_uwb_m1 bad1;
	uint8_t tiny[3] = {0x01u, 0x00u, 0x00u};

	T_EQ("m1.parse.short", aliro_dev_uwb_parse_m1(tiny, sizeof(tiny), &bad1), -1);
	/* an M3 message is not an M1: the header id guard must reject it */
	T_EQ("m1.parse.wrongid", aliro_dev_uwb_parse_m1(g_tx.msg->data, g_tx.msg->len, &bad1), -1);

	if (g_tx.msg) {
		aliro_uwb_session_message_free(g_tx.msg);
		g_tx.msg = NULL;
	}
	aliro_uwb_session_destroy(s);
	aliro_uwb_adapter_destroy(adapter);
	cherry_destroy_sync(cx);
}
