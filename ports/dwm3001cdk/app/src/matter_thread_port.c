/**
 * @file matter_thread_port.c — matter_thread.h on top of Zephyr's OpenThread.
 *
 * The dataset arrives from the commissioner as raw meshcop TLVs and
 * otDatasetSetActiveTlvs() takes raw meshcop TLVs, so nothing here has to
 * understand the format -- which is the point. This node parses exactly one
 * field out of it, the Extended PAN ID, and only so it can name the network
 * back to the commissioner.
 *
 * Built into every image. Without CONFIG_NET_L2_OPENTHREAD it refuses honestly
 * rather than disappearing: matter_clusters.c calls it unconditionally, and a
 * link error would be a worse way to learn that Thread was configured out.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "matter_thread.h"

/* For MATTER_INSTANCE_NAME_LEN: the SRP instance name is sized by the thing
 * that produces it, matter_fabric_instance_name(). */
#include "matter_case.h"
#include "matter_fabric.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(matter_thread, CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL);

#if defined(CONFIG_NET_L2_OPENTHREAD)

#include <openthread.h>
#include <openthread/dataset.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#include <stdio.h>
#include <string.h>

/** How often to look at the role while waiting. */
#define ATTACH_POLL_MS 250u

int matter_thread_start(const uint8_t *dataset, size_t len)
{
	otInstance *ot = openthread_get_default_instance();
	otOperationalDatasetTlvs tlvs;
	otError err;

	if (ot == NULL || dataset == NULL || len == 0u || len > sizeof(tlvs.mTlvs)) {
		return MATTER_E_INVAL;
	}

	memcpy(tlvs.mTlvs, dataset, len);
	tlvs.mLength = (uint8_t)len;

	openthread_mutex_lock();
	err = otDatasetSetActiveTlvs(ot, &tlvs);
	openthread_mutex_unlock();
	if (err != OT_ERROR_NONE) {
		LOG_ERR("dataset rejected by OpenThread (%d)", err);
		return MATTER_E_INVAL;
	}

	/*
	 * openthread_run() takes the mutex itself, so it must be called with the
	 * lock released. It enables IPv6 and then the Thread interface, which is
	 * when attaching actually begins.
	 */
	if (openthread_run() != 0) {
		LOG_ERR("OpenThread refused to start");
		return MATTER_E_STATE;
	}

	LOG_INF("OpenThread started on the commissioner's dataset (%u B)", (unsigned int)len);
	return MATTER_OK;
}

/**
 * Is this an address something off the Thread mesh could route to?
 *
 * NOT a test for "is it global". A border router's off-mesh-routable prefix is
 * very often a unique-local one, indistinguishable from the mesh-local prefix
 * by its first byte -- an earlier version of this checked fc00::/7 and would
 * have reported a perfectly routable OMR address as unreachable. The only
 * sound test is against the mesh-local prefix this network actually uses.
 */
static bool addr_is_offmesh(otInstance *ot, const otNetifAddress *a)
{
	const otMeshLocalPrefix *ml = otThreadGetMeshLocalPrefix(ot);

	/* fe80::/10 */
	if (a->mAddress.mFields.m8[0] == 0xFEu && (a->mAddress.mFields.m8[1] & 0xC0u) == 0x80u) {
		return false;
	}
	if (ml != NULL && memcmp(a->mAddress.mFields.m8, ml->m8, OT_MESH_LOCAL_PREFIX_SIZE) == 0) {
		return false;
	}
	return true;
}

static int count_offmesh(otInstance *ot)
{
	const otNetifAddress *a;
	int n = 0;

	for (a = otIp6GetUnicastAddresses(ot); a != NULL; a = a->mNext) {
		if (a->mPreferred && addr_is_offmesh(ot, a)) {
			n++;
		}
	}
	return n;
}

/**
 * Every address this node holds, and whether any of them is reachable.
 *
 * A registered SRP name is not the same as a reachable node. Auto host address
 * mode publishes the PREFERRED unicast addresses, and falls back to the
 * mesh-local EID when there are none -- and a mesh-local address does not leave
 * the Thread mesh, so a commissioner on Wi-Fi resolves the name and then routes
 * nowhere. That failure is invisible from the SRP result, which is why it is
 * printed here instead of assumed.
 */
static void log_addresses(otInstance *ot)
{
	const otNetifAddress *a;
	char buf[OT_IP6_ADDRESS_STRING_SIZE];

	for (a = otIp6GetUnicastAddresses(ot); a != NULL; a = a->mNext) {
		otIp6AddressToString(&a->mAddress, buf, sizeof(buf));
		LOG_DBG("  addr %s  preferred=%d %s", buf, (int)a->mPreferred,
			addr_is_offmesh(ot, a) ? "(off-mesh)" : "(local)");
	}
	if (count_offmesh(ot) == 0) {
		LOG_WRN("NO off-mesh-routable address -- the border router is publishing no "
			"prefix this node can autoconfigure from");
	}
}

int matter_thread_wait_attached(uint32_t timeout_ms)
{
	otInstance *ot = openthread_get_default_instance();
	uint32_t waited = 0u;
	bool announced = false;

	for (;;) {
		otDeviceRole role;
		int offmesh;

		openthread_mutex_lock();
		role = otThreadGetDeviceRole(ot);
		offmesh = count_offmesh(ot);
		openthread_mutex_unlock();

		if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
		    role == OT_DEVICE_ROLE_LEADER) {
			if (!announced) {
				LOG_INF("Thread attached after %u ms, role %d", waited, (int)role);
				announced = true;
			}
			/*
			 * Attached is NOT reachable. The off-mesh-routable
			 * address is autoconfigured from a prefix the border
			 * router publishes in network data, which arrives some
			 * time AFTER the attach -- and a node that registers SRP
			 * before it exists publishes only its mesh-local
			 * address, which no commissioner off the mesh can route
			 * to. Waiting here is what makes ConnectNetwork's
			 * Success mean something.
			 */
			if (offmesh > 0) {
				LOG_INF("reachable after %u ms (%d off-mesh address(es))", waited,
					offmesh);
				return MATTER_OK;
			}
		}
		if (waited >= timeout_ms) {
			if (announced) {
				LOG_WRN("attached but STILL no off-mesh address after %u ms",
					waited);
			} else {
				LOG_WRN("Thread still %s after %u ms",
					role == OT_DEVICE_ROLE_DETACHED ? "detached" : "disabled",
					waited);
			}
			openthread_mutex_lock();
			log_addresses(ot);
			openthread_mutex_unlock();
			return MATTER_E_TIMEOUT;
		}

		k_msleep(ATTACH_POLL_MS);
		waited += ATTACH_POLL_MS;
	}
}

/*
 * Everything the SRP client is handed must OUTLIVE the call. otSrpClientAddService()
 * links the service into a list it keeps and does not copy the strings, so a
 * stack buffer here would be a use-after-return that shows up as a garbled
 * service name on somebody else's border router.
 */
static char s_host_name[17];
static char s_instance_name[MATTER_INSTANCE_NAME_LEN];
static char s_service_type[] = "_matter._tcp";
static char s_txt_sii[] = "3000";
static char s_txt_sai[] = "300";
static char s_subtype[19]; /* "_I" + 16 hex digits + NUL */
static const char *s_subtype_labels[2];
static otSrpClientService s_service;
static otDnsTxtEntry s_txt[2];

static otUdpSocket s_udp;
static bool s_udp_open;

/**
 * Whatever arrives on the operational port, reported and dropped.
 *
 * There is no CASE responder yet, so this cannot answer. What it can do is
 * prove the half that would otherwise be invisible: a datagram here means the
 * commissioner resolved this node's name through the border router and routed
 * to it. Without it, "stuck at connecting" cannot be told apart from a service
 * that was never registered.
 */
static void udp_rx(void *ctx, otMessage *msg, const otMessageInfo *info)
{
	/*
	 * Sized for a Sigma3, not a Sigma1. A Sigma1 is 196 bytes, but a Sigma3
	 * carries the initiator's whole certificate chain encrypted -- with an
	 * intermediate certificate present that is comfortably past 512, and the
	 * only symptom of an undersized buffer here is this function declining to
	 * look at the message that ends the handshake.
	 *
	 * Static because this runs on OpenThread's own thread, whose stack is one
	 * of the two things deliberately left un-shrunk.
	 */
	static uint8_t buf[MATTER_CASE_SIGMA3_MAX];
	/* Sigma2 is the largest thing this sends: two certificates, a signature
	 * and the framing. Static for the same reason as the receive buffer. */
	static uint8_t reply[MATTER_CASE_SIGMA2_MAX];
	otMessageInfo reply_info;
	otMessage *out;
	size_t reply_len;
	uint16_t len = otMessageGetLength(msg) - otMessageGetOffset(msg);

	ARG_UNUSED(ctx);

	LOG_INF("UDP %u B on port %u from peer port %u", (unsigned int)len,
		(unsigned int)info->mSockPort, (unsigned int)info->mPeerPort);

	if (len > sizeof(buf)) {
		LOG_WRN("  too large to look at (%u B)", (unsigned int)len);
		return;
	}
	if (otMessageRead(msg, otMessageGetOffset(msg), buf, len) != len) {
		LOG_WRN("  could not be read out");
		return;
	}

	reply_len = matter_thread_on_datagram(buf, len, reply, sizeof(reply));
	if (reply_len == 0u) {
		return;
	}

	/*
	 * Replying to where it came FROM, rather than to the address SRP
	 * published. They are the same today and need not be: a commissioner
	 * behind a border router reaches this node from whichever of its
	 * addresses routes, and answering anywhere else answers a different
	 * peer.
	 */
	memset(&reply_info, 0, sizeof(reply_info));
	reply_info.mPeerAddr = info->mPeerAddr;
	reply_info.mPeerPort = info->mPeerPort;
	reply_info.mSockAddr = info->mSockAddr;

	out = otUdpNewMessage(openthread_get_default_instance(), NULL);
	if (out == NULL) {
		LOG_ERR("  no message buffer for the reply");
		return;
	}
	if (otMessageAppend(out, reply, (uint16_t)reply_len) != OT_ERROR_NONE ||
	    otUdpSend(openthread_get_default_instance(), &s_udp, out, &reply_info) !=
		    OT_ERROR_NONE) {
		/* otUdpSend takes ownership on success only. */
		otMessageFree(out);
		LOG_ERR("  reply could not be sent");
		return;
	}
	LOG_INF("  replied %u B", (unsigned int)reply_len);
}

/**
 * Every address this node holds, and whether any of them is reachable.
 *
 * A registered SRP name is not the same as a reachable node. Auto host address
 * mode publishes the PREFERRED unicast addresses, and falls back to the
 * mesh-local EID when there are none -- and a mesh-local address does not leave
 * the Thread mesh, so a commissioner on Wi-Fi resolves the name and then routes
 * nowhere. That failure is invisible from the SRP result, which is why it is
 * printed here instead of assumed.
 *
 * The address to look for is an off-mesh-routable one, which only exists if the
 * border router is publishing a prefix this node has picked up.
 */
/** The SRP server's verdict, which otSrpClientAddService() cannot give. */
static void srp_cb(otError err, const otSrpClientHostInfo *host, const otSrpClientService *services,
		   const otSrpClientService *removed, void *ctx)
{
	ARG_UNUSED(removed);
	ARG_UNUSED(ctx);

	if (err == OT_ERROR_NONE) {
		LOG_INF("SRP registered: host state %d, service state %d",
			host != NULL ? (int)host->mState : -1,
			services != NULL ? (int)services->mState : -1);
	} else {
		LOG_ERR("SRP registration FAILED (%d) -- the commissioner cannot resolve this node",
			err);
	}
	log_addresses(openthread_get_default_instance());
}

int matter_thread_advertise(const char *instance_name, uint16_t port)
{
	otInstance *ot = openthread_get_default_instance();
	otExtAddress eui;
	otSockAddr bind_addr;
	otError err;

	if (ot == NULL || instance_name == NULL) {
		return MATTER_E_INVAL;
	}
	if (strlen(instance_name) >= sizeof(s_instance_name)) {
		return MATTER_E_INVAL;
	}
	strcpy(s_instance_name, instance_name);

	/* The host name only has to be unique on the SRP server, and the EUI-64
	 * already is. */
	otPlatRadioGetIeeeEui64(ot, eui.m8);
	(void)snprintf(s_host_name, sizeof(s_host_name), "%02X%02X%02X%02X%02X%02X%02X%02X",
		       eui.m8[0], eui.m8[1], eui.m8[2], eui.m8[3], eui.m8[4], eui.m8[5], eui.m8[6],
		       eui.m8[7]);

	/*
	 * SII and SAI are the peer's retransmission timers, in milliseconds.
	 * They are optional and this node is not: it is a sleepy end device
	 * polling its parent every 3,000 ms, and a peer left on the 500 ms
	 * default would retransmit five times into a radio that is asleep and
	 * conclude the node is gone. Announcing the poll period is what makes
	 * the next exchange survivable.
	 */
	s_txt[0].mKey = "SII";
	s_txt[0].mValue = (const uint8_t *)s_txt_sii;
	s_txt[0].mValueLength = (uint16_t)strlen(s_txt_sii);
	s_txt[1].mKey = "SAI";
	s_txt[1].mValue = (const uint8_t *)s_txt_sai;
	s_txt[1].mValueLength = (uint16_t)strlen(s_txt_sai);

	/*
	 * The compressed-fabric subtype, "_I" + the id in uppercase hex
	 * (lib/dnssd/ServiceNaming.cpp, MakeServiceSubtype, kCompressedFabricId).
	 * A commissioner that already knows the node id resolves the instance
	 * name directly and does not need this; one that BROWSES for every node
	 * on its fabric finds nothing without it, and which of the two Apple
	 * does is not something this end can see.
	 *
	 * The first 16 characters of the instance name are that id already.
	 */
	s_subtype[0] = '_';
	s_subtype[1] = 'I';
	memcpy(&s_subtype[2], s_instance_name, 16u);
	s_subtype[18] = '\0';
	s_subtype_labels[0] = s_subtype;
	s_subtype_labels[1] = NULL;

	memset(&s_service, 0, sizeof(s_service));
	s_service.mName = s_service_type;
	s_service.mInstanceName = s_instance_name;
	s_service.mSubTypeLabels = s_subtype_labels;
	s_service.mPort = port;
	s_service.mTxtEntries = s_txt;
	s_service.mNumTxtEntries = 2u;

	openthread_mutex_lock();

	/*
	 * BIND BEFORE PUBLISHING. The commissioner starts resolving the moment
	 * the registration lands, and a name that resolves to a closed port is
	 * worse than one that has not appeared yet -- it turns a retry into a
	 * refusal.
	 */
	if (!s_udp_open) {
		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.mPort = port;
		if (otUdpOpen(ot, &s_udp, udp_rx, NULL) == OT_ERROR_NONE &&
		    otUdpBind(ot, &s_udp, &bind_addr, OT_NETIF_THREAD) == OT_ERROR_NONE) {
			s_udp_open = true;
			LOG_INF("listening on UDP %u", (unsigned int)port);
		} else {
			LOG_ERR("could not listen on UDP %u", (unsigned int)port);
		}
	}

	otSrpClientSetCallback(ot, srp_cb, NULL);
	err = otSrpClientSetHostName(ot, s_host_name);
	if (err == OT_ERROR_NONE) {
		err = otSrpClientEnableAutoHostAddress(ot);
	}
	if (err == OT_ERROR_NONE) {
		err = otSrpClientAddService(ot, &s_service);
	}
	if (err == OT_ERROR_NONE) {
		/* Finds the border router's SRP server itself, from the network
		 * data it already has as a child. */
		otSrpClientEnableAutoStartMode(ot, NULL, NULL);
	}
	openthread_mutex_unlock();

	if (err != OT_ERROR_NONE) {
		LOG_ERR("SRP registration refused (%d)", err);
		return MATTER_E_STATE;
	}

	LOG_INF("SRP: %s.%s._matter._tcp on %s.local port %u", s_instance_name, s_subtype,
		s_host_name, port);
	log_addresses(ot);
	return MATTER_OK;
}

#else /* !CONFIG_NET_L2_OPENTHREAD */

int matter_thread_advertise(const char *instance_name, uint16_t port)
{
	ARG_UNUSED(instance_name);
	ARG_UNUSED(port);

	return MATTER_E_STATE;
}

int matter_thread_start(const uint8_t *dataset, size_t len)
{
	ARG_UNUSED(dataset);
	ARG_UNUSED(len);

	LOG_WRN("Thread dataset received, but this image has no Thread stack");
	return MATTER_E_STATE;
}

int matter_thread_wait_attached(uint32_t timeout_ms)
{
	ARG_UNUSED(timeout_ms);

	return MATTER_E_TIMEOUT;
}

#endif /* CONFIG_NET_L2_OPENTHREAD */
