/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_clusters.h.
 *
 * status() and value() dispatch over the same paths and are kept as two
 * switches rather than one table of function pointers. A table would cost a
 * relocation and an indirect call per attribute on a part where flash is the
 * binding constraint, and the duplication is a case label -- the compiler
 * checks neither way, but a missing case here reads as a missing case.
 */
#include "matter_clusters.h"

#include <stddef.h>
#include <string.h>

/* GeneralCommissioning/Structs.h:41-44 */
#define TAG_BCI_FAILSAFE_EXPIRY 0u
#define TAG_BCI_FAILSAFE_MAX    1u

/* OperationalCredentials Structs.h:43-49, FabricDescriptorStruct. */
#define TAG_FABRIC_ROOT_KEY  1u
#define TAG_FABRIC_VENDOR_ID 2u
#define TAG_FABRIC_FABRIC_ID 3u
#define TAG_FABRIC_NODE_ID   4u
#define TAG_FABRIC_LABEL     5u
/** Every fabric-scoped struct carries this, always at 254. */
#define TAG_FABRIC_INDEX     254u

/* Same header, NOCStruct at :84-87. */
#define TAG_NOC_NOC  1u
#define TAG_NOC_ICAC 2u

/* BasicInformation/Structs.h:43-44, CapabilityMinimaStruct. */
#define TAG_CAPMIN_CASE_SESSIONS  0u
#define TAG_CAPMIN_SUBSCRIPTIONS  1u

/*
 * Strings this node reports about itself. Build-time, not per-device: this port
 * has no factory data partition and no per-board serial to read out of one.
 */
#define MATTER_VENDOR_NAME   "openaliro"
#define MATTER_PRODUCT_NAME  "DWM3001CDK Aliro Reader"
#define MATTER_SERIAL_NUMBER "DWM3001CDK-0001"

/* Descriptor/Structs.h:43-44, DeviceTypeStruct. */
#define TAG_DEVTYPE_TYPE     0u
#define TAG_DEVTYPE_REVISION 1u

/**
 * Every cluster the root endpoint serves, for Descriptor's ServerList.
 *
 * Descriptor is in it: a controller that reads ServerList and does not find the
 * cluster it just read is entitled to conclude the answer is stale.
 */
static const uint32_t k_root_servers[] = {
	MATTER_CLUSTER_DESCRIPTOR,       MATTER_CLUSTER_ACCESS_CONTROL,
	MATTER_CLUSTER_BASIC_INFORMATION, MATTER_CLUSTER_GENERAL_COMMISSIONING,
	MATTER_CLUSTER_NETWORK_COMMISSIONING, MATTER_CLUSTER_OPERATIONAL_CREDENTIALS,
};

/**
 * Every cluster the lock endpoint serves.
 *
 * Descriptor again, because every endpoint has one: it is how a controller
 * learns what the endpoint IS without being told where to look.
 */
static const uint32_t k_lock_servers[] = {
	MATTER_CLUSTER_DESCRIPTOR,
	MATTER_CLUSTER_DOOR_LOCK,
};

/* NetworkInfoStruct (python clusters/Objects.py, NetworkInfoStruct). */
#define TAG_NETINFO_ID        0u
#define TAG_NETINFO_CONNECTED 1u

/**
 * The slot currently being provisioned, allocating one if needed.
 *
 * A commissioner builds a fabric across several commands -- the root arrives
 * before the NOC -- so the half-built slot must survive between them. A slot
 * with a root but no index yet is that one. NULL when every slot is taken,
 * which is what AddNOC reports as TABLE_FULL.
 */
static struct matter_fabric *fabric_pending(struct matter_device_info *info)
{
	size_t i;

	for (i = 0u; i < MATTER_SUPPORTED_FABRICS; i++) {
		if (info->fabrics[i].have_root && info->fabrics[i].index == 0u) {
			return &info->fabrics[i];
		}
	}
	for (i = 0u; i < MATTER_SUPPORTED_FABRICS; i++) {
		if (!info->fabrics[i].have_root && info->fabrics[i].index == 0u) {
			return &info->fabrics[i];
		}
	}
	return NULL;
}

/** How many fabrics hold a complete identity. */
static uint8_t fabric_count(const struct matter_device_info *info)
{
	uint8_t n = 0u;
	size_t i;

	for (i = 0u; i < MATTER_SUPPORTED_FABRICS; i++) {
		if (info->fabrics[i].index != 0u) {
			n++;
		}
	}
	return n;
}

/**
 * The lowest index not already taken. Indices are 1-based on the wire.
 *
 * Kept in one place because an off-by-one here reports one fabric's
 * certificate under another's index, which no peer can detect.
 */
static uint8_t fabric_next_index(const struct matter_device_info *info)
{
	uint8_t want;

	for (want = 1u; want <= MATTER_SUPPORTED_FABRICS; want++) {
		bool taken = false;
		size_t i;

		for (i = 0u; i < MATTER_SUPPORTED_FABRICS; i++) {
			if (info->fabrics[i].index == want) {
				taken = true;
			}
		}
		if (!taken) {
			return want;
		}
	}
	return 0u;
}

static bool has_cluster(void *ctx, uint16_t endpoint, uint32_t cluster)
{
	(void)ctx;

	if (endpoint == MATTER_ENDPOINT_LOCK) {
		return cluster == MATTER_CLUSTER_DESCRIPTOR ||
		       cluster == MATTER_CLUSTER_DOOR_LOCK;
	}
	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return false;
	}
	return cluster == MATTER_CLUSTER_BASIC_INFORMATION ||
	       cluster == MATTER_CLUSTER_GENERAL_COMMISSIONING ||
	       cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING ||
	       cluster == MATTER_CLUSTER_DESCRIPTOR ||
	       cluster == MATTER_CLUSTER_ACCESS_CONTROL ||
	       cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS;
}

/*
 * Every endpoint.
 *
 * This exists because Apple reads NetworkCommissioning with the endpoint
 * WILDCARDED, so a node that cannot expand an endpoint wildcard looks like a
 * node with no network interface anywhere -- which is where commissioning
 * stopped before this. Endpoint 1 is the Door Lock, and it must appear here as
 * well as in the root's PartsList: a wildcard read walks THIS list, so an
 * endpoint missing from it is invisible no matter what PartsList claims.
 */
static const uint16_t k_endpoints[] = {
	MATTER_ENDPOINT_ROOT,
	MATTER_ENDPOINT_LOCK,
};

static size_t list_endpoints(void *ctx, const uint16_t **out)
{
	(void)ctx;

	*out = k_endpoints;
	return sizeof(k_endpoints) / sizeof(k_endpoints[0]);
}

static uint8_t attr_status(void *ctx, uint16_t endpoint, uint32_t cluster, uint32_t attribute)
{
	(void)ctx;

	/*
	 * Endpoint, then cluster, then attribute. The ORDER is the answer:
	 * MetadataLookup.cpp:68-88 reports the outermost thing that is missing,
	 * so a bad endpoint must not be reported as a bad attribute.
	 */
	if (endpoint == MATTER_ENDPOINT_LOCK) {
		if (cluster == MATTER_CLUSTER_DESCRIPTOR) {
			switch (attribute) {
			case MATTER_ATTR_DESC_DEVICE_TYPE_LIST:
			case MATTER_ATTR_DESC_SERVER_LIST:
			case MATTER_ATTR_DESC_CLIENT_LIST:
			case MATTER_ATTR_DESC_PARTS_LIST:
				return MATTER_IM_STATUS_SUCCESS;
			default:
				return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
			}
		}
		if (cluster != MATTER_CLUSTER_DOOR_LOCK) {
			return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
		}
		switch (attribute) {
		case MATTER_ATTR_DL_LOCK_STATE:
		case MATTER_ATTR_DL_LOCK_TYPE:
		case MATTER_ATTR_DL_ACTUATOR_ENABLED:
		case MATTER_ATTR_DL_OPERATING_MODE:
		case MATTER_ATTR_DL_SUPPORTED_OPERATING_MODES:
		case MATTER_ATTR_DL_ALIRO_VERIFICATION_KEY:
		case MATTER_ATTR_DL_ALIRO_GROUP_ID:
		case MATTER_ATTR_DL_ALIRO_GROUP_SUB_ID:
		case MATTER_ATTR_DL_ALIRO_EXPEDITED_VERSIONS:
		case MATTER_ATTR_DL_ALIRO_GROUP_RESOLVING_KEY:
		case MATTER_ATTR_DL_ALIRO_BLE_UWB_VERSIONS:
		case MATTER_ATTR_DL_ALIRO_BLE_ADV_VERSION:
		case MATTER_ATTR_DL_ALIRO_ISSUER_KEYS_MAX:
		case MATTER_ATTR_DL_ALIRO_ENDPOINT_KEYS_MAX:
		case MATTER_ATTR_DL_USERS_MAX:
		case MATTER_ATTR_DL_CREDS_PER_USER_MAX:
		/*
		 * FeatureMap is answered here for the same reason it is on
		 * NetworkCommissioning: it is what a controller reads to decide
		 * whether this lock is an Aliro reader, and without it the
		 * Aliro attributes below look like a lock that answers
		 * questions nobody asked.
		 */
		case MATTER_ATTR_FEATURE_MAP:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	}
	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT;
	}

	switch (cluster) {
	case MATTER_CLUSTER_BASIC_INFORMATION:
		switch (attribute) {
		case MATTER_ATTR_BASIC_DATA_MODEL_REVISION:
		case MATTER_ATTR_BASIC_VENDOR_NAME:
		case MATTER_ATTR_BASIC_VENDOR_ID:
		case MATTER_ATTR_BASIC_PRODUCT_NAME:
		case MATTER_ATTR_BASIC_PRODUCT_ID:
		case MATTER_ATTR_BASIC_NODE_LABEL:
		case MATTER_ATTR_BASIC_LOCATION:
		case MATTER_ATTR_BASIC_HARDWARE_VERSION:
		case MATTER_ATTR_BASIC_HARDWARE_VERSION_STR:
		case MATTER_ATTR_BASIC_SOFTWARE_VERSION:
		case MATTER_ATTR_BASIC_SOFTWARE_VERSION_STR:
		case MATTER_ATTR_BASIC_SERIAL_NUMBER:
		case MATTER_ATTR_BASIC_UNIQUE_ID:
		case MATTER_ATTR_BASIC_CAPABILITY_MINIMA:
		case MATTER_ATTR_BASIC_SPECIFICATION_VERSION:
		case MATTER_ATTR_BASIC_MAX_PATHS_PER_INVOKE:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_NETWORK_COMMISSIONING:
		switch (attribute) {
		case MATTER_ATTR_NC_MAX_NETWORKS:
		case MATTER_ATTR_NC_NETWORKS:
		case MATTER_ATTR_NC_SCAN_MAX_TIME_S:
		case MATTER_ATTR_NC_CONNECT_MAX_TIME_S:
		case MATTER_ATTR_NC_INTERFACE_ENABLED:
		case MATTER_ATTR_NC_LAST_NETWORKING_STATUS:
		case MATTER_ATTR_FEATURE_MAP:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_DESCRIPTOR:
		switch (attribute) {
		case MATTER_ATTR_DESC_DEVICE_TYPE_LIST:
		case MATTER_ATTR_DESC_SERVER_LIST:
		case MATTER_ATTR_DESC_CLIENT_LIST:
		case MATTER_ATTR_DESC_PARTS_LIST:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_ACCESS_CONTROL:
		switch (attribute) {
		case MATTER_ATTR_AC_ACL:
		case MATTER_ATTR_AC_SUBJECTS_PER_ENTRY:
		case MATTER_ATTR_AC_TARGETS_PER_ENTRY:
		case MATTER_ATTR_AC_ENTRIES_PER_FABRIC:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_OPERATIONAL_CREDENTIALS:
		switch (attribute) {
		case MATTER_ATTR_OC_NOCS:
		case MATTER_ATTR_OC_FABRICS:
		case MATTER_ATTR_OC_SUPPORTED_FABRICS:
		case MATTER_ATTR_OC_COMMISSIONED_FABRICS:
		case MATTER_ATTR_OC_TRUSTED_ROOTS:
		case MATTER_ATTR_OC_CURRENT_FABRIC_INDEX:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	case MATTER_CLUSTER_GENERAL_COMMISSIONING:
		switch (attribute) {
		case MATTER_ATTR_GC_BREADCRUMB:
		case MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO:
		case MATTER_ATTR_GC_REGULATORY_CONFIG:
		case MATTER_ATTR_GC_LOCATION_CAPABILITY:
		case MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION:
			return MATTER_IM_STATUS_SUCCESS;
		default:
			/*
			 * IsCommissioningWithoutPower (0x000C) lands here, and a
			 * real iPhone does ask for it. Saying UNSUPPORTED
			 * ATTRIBUTE is the correct answer for a node that does
			 * not implement it, and the commissioner carries on.
			 */
			return MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE;
		}
	default:
		return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
	}
}

/**
 * One Aliro protocol version, as the two big-endian bytes the spec asks for.
 *
 * Both version lists carry the same single entry, so they share this. The
 * ESP32 lock encodes it the same way (aliro_reader_delegate.cpp:106-115) and
 * that is the port Apple Home has actually provisioned.
 */
static void put_protocol_version_list(struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	const uint8_t version[2] = {
		(uint8_t)(MATTER_ALIRO_PROTOCOL_VERSION >> 8),
		(uint8_t)(MATTER_ALIRO_PROTOCOL_VERSION & 0xFFu),
	};

	(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
	(void)matter_tlv_put_bytes(w, MATTER_TLV_ANON, version, sizeof(version));
	(void)matter_tlv_end_container(w);
}

/**
 * Endpoint 1: the Door Lock and its own Descriptor.
 *
 * Split out rather than folded into attr_value() because the two endpoints
 * share cluster IDs -- Descriptor is on both -- and a single flat switch on
 * cluster would answer the root's Descriptor for the lock.
 */
static void lock_attr_value(const struct matter_device_info *info, uint32_t cluster,
			    uint32_t attribute, struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	size_t i;

	if (cluster == MATTER_CLUSTER_DESCRIPTOR) {
		switch (attribute) {
		case MATTER_ATTR_DESC_DEVICE_TYPE_LIST:
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_start_container(w, MATTER_TLV_ANON,
							 MATTER_TLV_STRUCTURE);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_DEVTYPE_TYPE),
						 MATTER_DEVICE_TYPE_DOOR_LOCK);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_DEVTYPE_REVISION),
						 MATTER_DEVICE_TYPE_LOCK_REV);
			(void)matter_tlv_end_container(w);
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_DESC_SERVER_LIST:
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			for (i = 0u; i < sizeof(k_lock_servers) / sizeof(k_lock_servers[0]); i++) {
				(void)matter_tlv_put_u64(w, MATTER_TLV_ANON, k_lock_servers[i]);
			}
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_DESC_CLIENT_LIST:
		case MATTER_ATTR_DESC_PARTS_LIST:
			/* A leaf endpoint: nothing hangs off it and it binds
			 * nothing as a client. */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_end_container(w);
			return;
		default:
			return;
		}
	}

	if (cluster != MATTER_CLUSTER_DOOR_LOCK) {
		return;
	}

	switch (attribute) {
	case MATTER_ATTR_FEATURE_MAP:
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_FEATURE_ALIRO_PROVISIONING |
							 MATTER_DL_FEATURE_ALIRO_BLE_UWB |
							 MATTER_DL_FEATURE_USER);
		return;
	case MATTER_ATTR_DL_USERS_MAX:
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_USERS_MAX);
		return;
	case MATTER_ATTR_DL_CREDS_PER_USER_MAX:
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_CREDS_PER_USER_MAX);
		return;
	case MATTER_ATTR_DL_LOCK_STATE:
		/*
		 * Locked, and honest: this reader has no actuator and nothing
		 * it reports here moves a bolt. It exists because LockState is
		 * mandatory and a controller uses it to draw the tile.
		 */
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_LOCK_STATE_LOCKED);
		return;
	case MATTER_ATTR_DL_LOCK_TYPE:
		/* 0x00 is DeadBolt (DoorLock/Enums.h, DlLockType). */
		(void)matter_tlv_put_u64(w, tag, 0u);
		return;
	case MATTER_ATTR_DL_ACTUATOR_ENABLED:
		/*
		 * False. There IS no actuator on this board, and claiming one
		 * would invite a Lock/Unlock command this endpoint cannot
		 * honour. The Aliro half is what this device is for.
		 */
		(void)matter_tlv_put_bool(w, tag, false);
		return;
	case MATTER_ATTR_DL_OPERATING_MODE:
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_OPERATING_MODE_NORMAL);
		return;
	case MATTER_ATTR_DL_SUPPORTED_OPERATING_MODES:
		(void)matter_tlv_put_u64(w, tag, MATTER_DL_SUPPORTED_OPERATING_MODES);
		return;

	/*
	 * ---- the Aliro reader configuration -------------------------------
	 *
	 * NULL until SetAliroReaderConfig arrives, which is exactly what tells
	 * a controller this reader is unprovisioned and needs an identity.
	 * Reporting zeros instead would claim a key that cannot verify.
	 */
	case MATTER_ATTR_DL_ALIRO_VERIFICATION_KEY:
		if (info->have_aliro_reader_config) {
			(void)matter_tlv_put_bytes(w, tag, info->aliro_verification_key,
						   MATTER_ALIRO_VERIFICATION_KEY_LEN);
		} else {
			(void)matter_tlv_put_null(w, tag);
		}
		return;
	case MATTER_ATTR_DL_ALIRO_GROUP_ID:
		if (info->have_aliro_reader_config) {
			(void)matter_tlv_put_bytes(w, tag, info->aliro_group_id,
						   MATTER_ALIRO_GROUP_ID_LEN);
		} else {
			(void)matter_tlv_put_null(w, tag);
		}
		return;
	case MATTER_ATTR_DL_ALIRO_GROUP_RESOLVING_KEY:
		if (info->have_aliro_group_resolving_key) {
			(void)matter_tlv_put_bytes(w, tag, info->aliro_group_resolving_key,
						   MATTER_ALIRO_GROUP_ID_LEN);
		} else {
			(void)matter_tlv_put_null(w, tag);
		}
		return;
	case MATTER_ATTR_DL_ALIRO_GROUP_SUB_ID:
		/* Not nullable and not provisioned: it identifies the reader
		 * GROUP this device belongs to, which it has before any
		 * controller talks to it. The port supplies it. */
		(void)matter_tlv_put_bytes(w, tag, info->aliro_group_sub_id,
					   MATTER_ALIRO_GROUP_ID_LEN);
		return;
	case MATTER_ATTR_DL_ALIRO_EXPEDITED_VERSIONS:
	case MATTER_ATTR_DL_ALIRO_BLE_UWB_VERSIONS:
		put_protocol_version_list(w, tag);
		return;
	case MATTER_ATTR_DL_ALIRO_BLE_ADV_VERSION:
		(void)matter_tlv_put_u64(w, tag, MATTER_ALIRO_BLE_ADV_VERSION);
		return;
	case MATTER_ATTR_DL_ALIRO_ISSUER_KEYS_MAX:
	case MATTER_ATTR_DL_ALIRO_ENDPOINT_KEYS_MAX:
		(void)matter_tlv_put_u64(w, tag, MATTER_ALIRO_KEYS_SUPPORTED);
		return;
	default:
		return;
	}
}

static void attr_value(void *ctx, uint16_t endpoint, uint32_t cluster, uint32_t attribute,
		       struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	const struct matter_device_info *info = (const struct matter_device_info *)ctx;

	/*
	 * The lock endpoint is answered first and returns, so everything below
	 * it can still assume the root. attr_status() has already refused any
	 * endpoint that is neither.
	 */
	if (endpoint == MATTER_ENDPOINT_LOCK) {
		lock_attr_value(info, cluster, attribute, w, tag);
		return;
	}

	if (cluster == MATTER_CLUSTER_BASIC_INFORMATION) {
		switch (attribute) {
		case MATTER_ATTR_BASIC_DATA_MODEL_REVISION:
			(void)matter_tlv_put_u64(w, tag, MATTER_DATA_MODEL_REVISION);
			return;
		case MATTER_ATTR_BASIC_VENDOR_NAME:
			(void)matter_tlv_put_utf8(w, tag, MATTER_VENDOR_NAME, strlen(MATTER_VENDOR_NAME));
			return;
		case MATTER_ATTR_BASIC_VENDOR_ID:
			(void)matter_tlv_put_u64(w, tag, info->vendor_id);
			return;
		case MATTER_ATTR_BASIC_PRODUCT_NAME:
			(void)matter_tlv_put_utf8(w, tag, MATTER_PRODUCT_NAME,
						  strlen(MATTER_PRODUCT_NAME));
			return;
		case MATTER_ATTR_BASIC_PRODUCT_ID:
			(void)matter_tlv_put_u64(w, tag, info->product_id);
			return;
		case MATTER_ATTR_BASIC_NODE_LABEL:
			/* Writable, and empty until somebody writes one. A
			 * controller supplies its own name for the accessory. */
			(void)matter_tlv_put_utf8(w, tag, "", 0u);
			return;
		case MATTER_ATTR_BASIC_LOCATION:
			/* "XX" is the spec's value for "not configured", and it
			 * has to be exactly two characters. */
			(void)matter_tlv_put_utf8(w, tag, "XX", 2u);
			return;
		case MATTER_ATTR_BASIC_HARDWARE_VERSION:
		case MATTER_ATTR_BASIC_SOFTWARE_VERSION:
			(void)matter_tlv_put_u64(w, tag, 1u);
			return;
		case MATTER_ATTR_BASIC_HARDWARE_VERSION_STR:
		case MATTER_ATTR_BASIC_SOFTWARE_VERSION_STR:
			(void)matter_tlv_put_utf8(w, tag, "1", 1u);
			return;
		case MATTER_ATTR_BASIC_SERIAL_NUMBER:
		case MATTER_ATTR_BASIC_UNIQUE_ID:
			/*
			 * The same string for both, and it is a BUILD-TIME
			 * constant: this port has no per-device serial to read.
			 * Two boards running this image are indistinguishable
			 * here, which matters the moment a home holds both.
			 */
			(void)matter_tlv_put_utf8(w, tag, MATTER_SERIAL_NUMBER,
						  strlen(MATTER_SERIAL_NUMBER));
			return;
		case MATTER_ATTR_BASIC_CAPABILITY_MINIMA:
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CAPMIN_CASE_SESSIONS),
						 MATTER_CASE_SESSIONS_PER_FABRIC);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CAPMIN_SUBSCRIPTIONS),
						 MATTER_SUBSCRIPTIONS_PER_FABRIC);
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_BASIC_SPECIFICATION_VERSION:
			(void)matter_tlv_put_u64(w, tag, MATTER_SPECIFICATION_VERSION);
			return;
		case MATTER_ATTR_BASIC_MAX_PATHS_PER_INVOKE:
			(void)matter_tlv_put_u64(w, tag, MATTER_MAX_PATHS_PER_INVOKE);
			return;
		default:
			return;
		}
	}

	if (cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING) {
		switch (attribute) {
		case MATTER_ATTR_NC_MAX_NETWORKS:
			(void)matter_tlv_put_u64(w, tag, 1u);
			return;
		case MATTER_ATTR_NC_NETWORKS:
			/*
			 * A list of NetworkInfoStruct. Empty until a dataset
			 * arrives, and then exactly one entry whose networkID is
			 * the Extended PAN ID -- which is the id ConnectNetwork
			 * names the network by. `connected` is false and stays
			 * false: nothing here has joined anything.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			if (info->have_thread_xpanid) {
				(void)matter_tlv_start_container(w, MATTER_TLV_ANON,
								 MATTER_TLV_STRUCTURE);
				(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_NETINFO_ID),
							   info->thread_xpanid,
							   sizeof(info->thread_xpanid));
				(void)matter_tlv_put_bool(w, MATTER_TLV_CTX(TAG_NETINFO_CONNECTED),
							  false);
				(void)matter_tlv_end_container(w);
			}
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_NC_SCAN_MAX_TIME_S:
			/* Never scanned; the value still has to be inside the
			 * spec's 1..255 range to be a legal answer. */
			(void)matter_tlv_put_u64(w, tag, 30u);
			return;
		case MATTER_ATTR_NC_CONNECT_MAX_TIME_S:
			(void)matter_tlv_put_u64(w, tag, 60u);
			return;
		case MATTER_ATTR_NC_INTERFACE_ENABLED:
			(void)matter_tlv_put_bool(w, tag, true);
			return;
		case MATTER_ATTR_NC_LAST_NETWORKING_STATUS:
			(void)matter_tlv_put_u64(w, tag, info->last_network_status);
			return;
		case MATTER_ATTR_FEATURE_MAP:
			/*
			 * Thread, and only Thread. This is the answer Apple was
			 * asking for when it read this cluster with the endpoint
			 * wildcarded and got silence.
			 */
			(void)matter_tlv_put_u64(w, tag, MATTER_NC_FEATURE_THREAD);
			return;
		default:
			return;
		}
	}

	if (cluster == MATTER_CLUSTER_DESCRIPTOR) {
		size_t i;

		switch (attribute) {
		case MATTER_ATTR_DESC_DEVICE_TYPE_LIST:
			/*
			 * What this endpoint IS, which is the question a
			 * controller asks once it owns the node. Endpoint 0 is
			 * the Root Node and nothing else.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_start_container(w, MATTER_TLV_ANON,
							 MATTER_TLV_STRUCTURE);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_DEVTYPE_TYPE),
						 MATTER_DEVICE_TYPE_ROOT_NODE);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_DEVTYPE_REVISION),
						 MATTER_DEVICE_TYPE_ROOT_REV);
			(void)matter_tlv_end_container(w);
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_DESC_SERVER_LIST:
			/* Built from the same list has_cluster() answers from,
			 * so the two cannot drift into disagreeing about what
			 * this endpoint carries. */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			for (i = 0u; i < sizeof(k_root_servers) / sizeof(k_root_servers[0]); i++) {
				(void)matter_tlv_put_u64(w, MATTER_TLV_ANON, k_root_servers[i]);
			}
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_DESC_CLIENT_LIST:
			/* Empty, and empty is an answer: this node binds nothing
			 * as a client. */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_DESC_PARTS_LIST:
			/*
			 * The Door Lock. This is the attribute that turns an
			 * empty tile into a lock: a controller reads the root's
			 * PartsList to find the endpoints that carry function,
			 * and a Root Node on its own carries none.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_put_u64(w, MATTER_TLV_ANON, MATTER_ENDPOINT_LOCK);
			(void)matter_tlv_end_container(w);
			return;
		default:
			return;
		}
	}

	if (cluster == MATTER_CLUSTER_ACCESS_CONTROL) {
		switch (attribute) {
		case MATTER_ATTR_AC_ACL:
			/*
			 * Handed straight back as it arrived. Nothing here has
			 * decoded it -- see the note on matter_device_info.acl:
			 * this list is RECORDED, NOT ENFORCED.
			 */
			if (info->acl_len > 0u) {
				(void)matter_tlv_put_encoded(w, tag, info->acl, info->acl_len);
			} else {
				(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
				(void)matter_tlv_end_container(w);
			}
			return;
		case MATTER_ATTR_AC_SUBJECTS_PER_ENTRY:
		case MATTER_ATTR_AC_ENTRIES_PER_FABRIC:
			/* The spec's floor for both, and what one fabric needs. */
			(void)matter_tlv_put_u64(w, tag, 4u);
			return;
		case MATTER_ATTR_AC_TARGETS_PER_ENTRY:
			(void)matter_tlv_put_u64(w, tag, 3u);
			return;
		default:
			return;
		}
	}

	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		size_t fi;

		switch (attribute) {
		case MATTER_ATTR_OC_FABRICS:
			/*
			 * EVERY fabric, not just the first. A commissioner
			 * reads this to confirm the fabric it created is one
			 * this node joined -- and with two administrators there
			 * are two, so reporting one tells the other it was
			 * never adopted.
			 *
			 * Fabric-scoped in the spec: the answer should be
			 * filtered to the reading session's fabric. This does
			 * not filter, which over-reports rather than under-
			 * reports, and is written down because it is a gap.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			for (fi = 0u; fi < MATTER_SUPPORTED_FABRICS; fi++) {
				const struct matter_fabric *f = &info->fabrics[fi];

				if (f->index == 0u) {
					continue;
				}
				(void)matter_tlv_start_container(w, MATTER_TLV_ANON,
								 MATTER_TLV_STRUCTURE);
				(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_FABRIC_ROOT_KEY),
							   f->root_public_key,
							   MATTER_FABRIC_PUBKEY_LEN);
				(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_FABRIC_VENDOR_ID),
							 f->admin_vendor_id);
				(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_FABRIC_FABRIC_ID),
							 f->fabric_id);
				(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_FABRIC_NODE_ID),
							 f->node_id);
				/* Empty until a commissioner writes one. */
				(void)matter_tlv_put_utf8(w, MATTER_TLV_CTX(TAG_FABRIC_LABEL), "",
							  0u);
				(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_FABRIC_INDEX),
							 f->index);
				(void)matter_tlv_end_container(w);
			}
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_OC_NOCS:
			/*
			 * The certificates themselves. Fabric-scoped AND
			 * restricted to Administer in the spec; this node
			 * enforces neither, the same gap the ACL note records.
			 * Nothing here is secret -- a NOC is public -- but the
			 * restriction exists and is not honoured.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			for (fi = 0u; fi < MATTER_SUPPORTED_FABRICS; fi++) {
				const struct matter_fabric *f = &info->fabrics[fi];

				if (f->index == 0u) {
					continue;
				}
				(void)matter_tlv_start_container(w, MATTER_TLV_ANON,
								 MATTER_TLV_STRUCTURE);
				(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_NOC_NOC), f->noc,
							   f->noc_len);
				if (f->icac_len > 0u) {
					(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_NOC_ICAC),
								   info->icac.buf, f->icac_len);
				} else {
					/* Nullable, and null is the answer when
					 * the root signed the NOC directly. */
					(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_NOC_ICAC));
				}
				(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_FABRIC_INDEX),
							 f->index);
				(void)matter_tlv_end_container(w);
			}
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_OC_TRUSTED_ROOTS:
			/*
			 * A list of the root CERTIFICATES, which this node does
			 * not keep -- matter_fabric holds the root public key
			 * and discards the ~300 bytes around it. An empty list
			 * is the honest consequence of that choice.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_ARRAY);
			(void)matter_tlv_end_container(w);
			return;
		case MATTER_ATTR_OC_CURRENT_FABRIC_INDEX:
			/*
			 * The fabric of whoever is ASKING, which the port
			 * records from the session the request arrived on.
			 * Answering with the first live fabric was right while
			 * there was one and a guess once there were two -- and
			 * the guess is what made the home hub remove itself.
			 */
			(void)matter_tlv_put_u64(w, tag, info->accessing_fabric_index);
			return;
		case MATTER_ATTR_OC_SUPPORTED_FABRICS:
			(void)matter_tlv_put_u64(w, tag, MATTER_SUPPORTED_FABRICS);
			return;
		case MATTER_ATTR_OC_COMMISSIONED_FABRICS:
			(void)matter_tlv_put_u64(w, tag, fabric_count(info));
			return;
		default:
			return;
		}
	}

	switch (attribute) {
	case MATTER_ATTR_GC_BREADCRUMB:
		(void)matter_tlv_put_u64(w, tag, info->breadcrumb);
		return;
	case MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO:
		(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_BCI_FAILSAFE_EXPIRY),
					 info->failsafe_expiry_s);
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_BCI_FAILSAFE_MAX),
					 info->failsafe_max_s);
		(void)matter_tlv_end_container(w);
		return;
	case MATTER_ATTR_GC_REGULATORY_CONFIG:
		(void)matter_tlv_put_u64(w, tag, info->regulatory_config);
		return;
	case MATTER_ATTR_GC_LOCATION_CAPABILITY:
		(void)matter_tlv_put_u64(w, tag, info->location_capability);
		return;
	case MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION:
		(void)matter_tlv_put_bool(w, tag, info->supports_concurrent_connection);
		return;
	default:
		return;
	}
}

/*
 * Attribute lists for expanding a wildcard read. These are exactly the
 * attributes attr_status() answers SUCCESS for, and the two must agree: an id
 * here that attr_status() refuses turns a wildcard into a report full of
 * UNSUPPORTED_ATTRIBUTE, which is worse than the silence it replaced.
 *
 * The global attributes (FeatureMap 0xFFFC, ClusterRevision 0xFFFD and the
 * rest) are deliberately absent. Nothing has asked for them, and a wildcard
 * that names them commits this node to answering them individually too.
 */
static const uint32_t k_gc_attrs[] = {
	MATTER_ATTR_GC_BREADCRUMB,
	MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO,
	MATTER_ATTR_GC_REGULATORY_CONFIG,
	MATTER_ATTR_GC_LOCATION_CAPABILITY,
	MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION,
};

static const uint32_t k_basic_attrs[] = {
	MATTER_ATTR_BASIC_DATA_MODEL_REVISION,   MATTER_ATTR_BASIC_VENDOR_NAME,
	MATTER_ATTR_BASIC_VENDOR_ID,             MATTER_ATTR_BASIC_PRODUCT_NAME,
	MATTER_ATTR_BASIC_PRODUCT_ID,            MATTER_ATTR_BASIC_NODE_LABEL,
	MATTER_ATTR_BASIC_LOCATION,              MATTER_ATTR_BASIC_HARDWARE_VERSION,
	MATTER_ATTR_BASIC_HARDWARE_VERSION_STR,  MATTER_ATTR_BASIC_SOFTWARE_VERSION,
	MATTER_ATTR_BASIC_SOFTWARE_VERSION_STR,  MATTER_ATTR_BASIC_SERIAL_NUMBER,
	MATTER_ATTR_BASIC_UNIQUE_ID,             MATTER_ATTR_BASIC_CAPABILITY_MINIMA,
	MATTER_ATTR_BASIC_SPECIFICATION_VERSION, MATTER_ATTR_BASIC_MAX_PATHS_PER_INVOKE,
};

static const uint32_t k_desc_attrs[] = {
	MATTER_ATTR_DESC_DEVICE_TYPE_LIST,
	MATTER_ATTR_DESC_SERVER_LIST,
	MATTER_ATTR_DESC_CLIENT_LIST,
	MATTER_ATTR_DESC_PARTS_LIST,
};

/*
 * FeatureMap leads, because it is the attribute that decides what the rest
 * mean: without the two Aliro bits a controller reads 0x0080..0x0088 as
 * attributes a lock has no business answering.
 */
static const uint32_t k_lock_attrs[] = {
	MATTER_ATTR_FEATURE_MAP,
	MATTER_ATTR_DL_LOCK_STATE,
	MATTER_ATTR_DL_LOCK_TYPE,
	MATTER_ATTR_DL_ACTUATOR_ENABLED,
	MATTER_ATTR_DL_OPERATING_MODE,
	MATTER_ATTR_DL_SUPPORTED_OPERATING_MODES,
	MATTER_ATTR_DL_ALIRO_VERIFICATION_KEY,
	MATTER_ATTR_DL_ALIRO_GROUP_ID,
	MATTER_ATTR_DL_ALIRO_GROUP_SUB_ID,
	MATTER_ATTR_DL_ALIRO_EXPEDITED_VERSIONS,
	MATTER_ATTR_DL_ALIRO_GROUP_RESOLVING_KEY,
	MATTER_ATTR_DL_ALIRO_BLE_UWB_VERSIONS,
	MATTER_ATTR_DL_ALIRO_BLE_ADV_VERSION,
	MATTER_ATTR_DL_ALIRO_ISSUER_KEYS_MAX,
	MATTER_ATTR_DL_ALIRO_ENDPOINT_KEYS_MAX,
	MATTER_ATTR_DL_USERS_MAX,
	MATTER_ATTR_DL_CREDS_PER_USER_MAX,
};

static const uint32_t k_ac_attrs[] = {
	MATTER_ATTR_AC_ACL,
	MATTER_ATTR_AC_SUBJECTS_PER_ENTRY,
	MATTER_ATTR_AC_TARGETS_PER_ENTRY,
	MATTER_ATTR_AC_ENTRIES_PER_FABRIC,
};

static const uint32_t k_oc_attrs[] = {
	MATTER_ATTR_OC_NOCS,          MATTER_ATTR_OC_FABRICS,
	MATTER_ATTR_OC_SUPPORTED_FABRICS, MATTER_ATTR_OC_COMMISSIONED_FABRICS,
	MATTER_ATTR_OC_TRUSTED_ROOTS, MATTER_ATTR_OC_CURRENT_FABRIC_INDEX,
};

/*
 * FeatureMap is in this list where it is in no other, because it is the one
 * global attribute a commissioner cannot proceed without: it says which network
 * technologies exist. Listing it here commits this node to answering it for
 * THIS cluster only, which attr_status() above does.
 */
static const uint32_t k_nc_attrs[] = {
	MATTER_ATTR_NC_MAX_NETWORKS,      MATTER_ATTR_NC_NETWORKS,
	MATTER_ATTR_NC_SCAN_MAX_TIME_S,   MATTER_ATTR_NC_CONNECT_MAX_TIME_S,
	MATTER_ATTR_NC_INTERFACE_ENABLED, MATTER_ATTR_NC_LAST_NETWORKING_STATUS,
	MATTER_ATTR_FEATURE_MAP,
};

/**
 * Every cluster on @p endpoint, which is the same list has_cluster() answers
 * from and the same one Descriptor's ServerList reports. One array, so the
 * three cannot drift into disagreeing about what this endpoint carries.
 */
static size_t list_clusters(void *ctx, uint16_t endpoint, const uint32_t **out)
{
	(void)ctx;

	if (endpoint == MATTER_ENDPOINT_LOCK) {
		*out = k_lock_servers;
		return sizeof(k_lock_servers) / sizeof(k_lock_servers[0]);
	}
	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return 0u;
	}
	*out = k_root_servers;
	return sizeof(k_root_servers) / sizeof(k_root_servers[0]);
}

static size_t list_attrs(void *ctx, uint16_t endpoint, uint32_t cluster, const uint32_t **out)
{
	(void)ctx;

	if (endpoint == MATTER_ENDPOINT_LOCK) {
		if (cluster == MATTER_CLUSTER_DESCRIPTOR) {
			*out = k_desc_attrs;
			return sizeof(k_desc_attrs) / sizeof(k_desc_attrs[0]);
		}
		if (cluster == MATTER_CLUSTER_DOOR_LOCK) {
			*out = k_lock_attrs;
			return sizeof(k_lock_attrs) / sizeof(k_lock_attrs[0]);
		}
		return 0u;
	}
	if (endpoint != MATTER_ENDPOINT_ROOT) {
		return 0u;
	}
	if (cluster == MATTER_CLUSTER_BASIC_INFORMATION) {
		*out = k_basic_attrs;
		return sizeof(k_basic_attrs) / sizeof(k_basic_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_GENERAL_COMMISSIONING) {
		*out = k_gc_attrs;
		return sizeof(k_gc_attrs) / sizeof(k_gc_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_DESCRIPTOR) {
		*out = k_desc_attrs;
		return sizeof(k_desc_attrs) / sizeof(k_desc_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_ACCESS_CONTROL) {
		*out = k_ac_attrs;
		return sizeof(k_ac_attrs) / sizeof(k_ac_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		*out = k_oc_attrs;
		return sizeof(k_oc_attrs) / sizeof(k_oc_attrs[0]);
	}
	if (cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING) {
		*out = k_nc_attrs;
		return sizeof(k_nc_attrs) / sizeof(k_nc_attrs[0]);
	}
	return 0u;
}

/** Read one unsigned field out of a command's TLV arguments. */
static bool field_u64(const struct matter_im_invoke *inv, uint8_t tag, uint64_t *out)
{
	struct matter_tlv_reader r;

	if (!inv->has_fields || inv->fields == NULL) {
		return false;
	}
	matter_tlv_reader_init(&r, inv->fields, inv->fields_len);
	if (matter_tlv_next(&r) != MATTER_OK || !matter_tlv_is_container(&r)) {
		return false;
	}
	if (matter_tlv_enter(&r) != MATTER_OK) {
		return false;
	}
	for (;;) {
		int rc = matter_tlv_next(&r);

		if (rc != MATTER_OK) {
			return false;
		}
		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(tag)) {
			return matter_tlv_get_u64(&r, out) == MATTER_OK;
		}
	}
}

/* -------------------------------------- OperationalCredentials --- */

/*
 * Command field tags. Matter numbers a command's arguments from 0 in
 * declaration order, so these are the positions in
 * controller/python/matter/clusters/Objects.py, which spells the tag out
 * rather than leaving it to be counted.
 */
#define TAG_ATTEST_NONCE 0u
#define TAG_CERT_TYPE    0u
#define TAG_CSR_NONCE    0u

#define TAG_ADDNOC_NOC                0u
#define TAG_ADDNOC_ICAC               1u
#define TAG_ADDNOC_IPK                2u
#define TAG_ADDNOC_CASE_ADMIN_SUBJECT 3u
#define TAG_ADDNOC_ADMIN_VENDOR_ID    4u

#define TAG_ADDROOT_CERT 0u

/* Response field tags, same source. */
#define TAG_RESP_ELEMENTS  0u
#define TAG_RESP_SIGNATURE 1u
#define TAG_RESP_CERT      0u

#define TAG_NOCRESP_STATUS       0u
#define TAG_NOCRESP_FABRIC_INDEX 1u

/** Borrow one octet-string field out of a command's arguments. */
static bool field_bytes(const struct matter_im_invoke *inv, uint8_t tag, const uint8_t **out,
			size_t *len)
{
	struct matter_tlv_reader r;

	if (!inv->has_fields || inv->fields == NULL) {
		return false;
	}
	matter_tlv_reader_init(&r, inv->fields, inv->fields_len);
	if (matter_tlv_next(&r) != MATTER_OK || !matter_tlv_is_container(&r)) {
		return false;
	}
	if (matter_tlv_enter(&r) != MATTER_OK) {
		return false;
	}
	for (;;) {
		if (matter_tlv_next(&r) != MATTER_OK) {
			return false;
		}
		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(tag)) {
			return matter_tlv_get_bytes(&r, out, len) == MATTER_OK;
		}
	}
}

/* --------------------------------------- NetworkCommissioning --- */

/* AddOrUpdateThreadNetwork / ConnectNetwork field tags, and the two responses. */
#define TAG_ADDTHREAD_DATASET  0u
#define TAG_CONNECT_NETWORK_ID 0u
#define TAG_NCRESP_STATUS      0u
#define TAG_NCRESP_INDEX       2u
#define TAG_CONNRESP_STATUS    0u
#define TAG_CONNRESP_ERROR     2u

/**
 * Thread meshcop TLV type for the Extended PAN ID (Thread 1.3 spec, 8.10.1.5).
 *
 * The operational dataset is a sequence of one-byte type, one-byte length,
 * value -- a different encoding from everything else here, and unrelated to
 * Matter TLV.
 */
#define MESHCOP_TLV_EXTENDED_PANID 0x02u

/**
 * Find the Extended PAN ID in a Thread operational dataset.
 *
 * Walked rather than indexed: the dataset's TLVs may arrive in any order, and a
 * length that runs past the end is a malformed dataset rather than a reason to
 * read past the buffer.
 */
static bool dataset_xpanid(const uint8_t *ds, size_t len, uint8_t out[MATTER_THREAD_XPANID_LEN])
{
	size_t i = 0u;

	while (i + 2u <= len) {
		uint8_t type = ds[i];
		size_t vlen = ds[i + 1u];

		if (i + 2u + vlen > len) {
			return false;
		}
		if (type == MESHCOP_TLV_EXTENDED_PANID && vlen == MATTER_THREAD_XPANID_LEN) {
			memcpy(out, &ds[i + 2u], MATTER_THREAD_XPANID_LEN);
			return true;
		}
		i += 2u + vlen;
	}
	return false;
}

/**
 * Publish "<compressed-fabric-id>-<node-id>._matter._tcp" over SRP.
 *
 * Silent when there is no fabric yet: a node with a network but no identity has
 * no name to register under, and that is a legal intermediate state rather than
 * a failure. Failures ARE logged by the port, which is where the SRP server's
 * answer eventually lands.
 */
static void advertise_one(const struct matter_fabric *fabric);

static void advertise_operational(const struct matter_device_info *info)
{
	char name[MATTER_INSTANCE_NAME_LEN];
	size_t fi;

	/*
	 * One instance PER FABRIC. The name is derived from the compressed
	 * fabric id and this node's id on that fabric, so a second
	 * administrator resolving the first fabric's name finds an address it
	 * cannot open a session to.
	 */
	for (fi = 0u; fi < MATTER_SUPPORTED_FABRICS; fi++) {
		if (info->fabrics[fi].index != 0u) {
			advertise_one(&info->fabrics[fi]);
		}
	}
}

static void advertise_one(const struct matter_fabric *fabric)
{
	char name[MATTER_INSTANCE_NAME_LEN];

	if (matter_fabric_instance_name(fabric, name, sizeof(name)) != MATTER_OK) {
		return;
	}
	(void)matter_thread_advertise(name, MATTER_OPERATIONAL_PORT);
}

/**
 * Run one NetworkCommissioning command.
 *
 * @return the IM status. The networking verdict goes in last_network_status and
 *         travels in the response payload, the same split AddNOC uses.
 */
static uint8_t network_command(struct matter_device_info *info, const struct matter_im_invoke *inv,
			       uint32_t *response_command)
{
	const uint8_t *v = NULL;
	size_t v_len = 0u;

	if (!info->failsafe_armed) {
		return MATTER_IM_STATUS_FAILSAFE_REQUIRED;
	}

	switch (inv->command) {
	case MATTER_CMD_NC_ADD_OR_UPDATE_THREAD_NETWORK:
		*response_command = MATTER_CMD_NC_NETWORK_CONFIG_RESPONSE;
		if (!field_bytes(inv, TAG_ADDTHREAD_DATASET, &v, &v_len) || v_len == 0u ||
		    v_len > MATTER_THREAD_DATASET_MAX) {
			info->last_network_status = MATTER_NC_STATUS_OUT_OF_RANGE;
			return MATTER_IM_STATUS_SUCCESS;
		}
		memcpy(info->thread_dataset, v, v_len);
		info->thread_dataset_len = v_len;
		info->have_thread_xpanid =
			dataset_xpanid(info->thread_dataset, v_len, info->thread_xpanid);
		/*
		 * Start attaching HERE rather than at ConnectNetwork. The
		 * commissioner sends ArmFailSafe in between and a Thread attach
		 * costs seconds, so the round trip is free progress. Storing the
		 * dataset succeeds either way: the commissioner asked this node
		 * to remember a network, and it has.
		 */
		info->thread_started = matter_thread_start(v, v_len) == MATTER_OK;
		info->last_network_status = MATTER_NC_STATUS_SUCCESS;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_NC_CONNECT_NETWORK:
		*response_command = MATTER_CMD_NC_CONNECT_NETWORK_RESPONSE;
		if (!info->thread_started) {
			/* Nothing is attaching, so nothing will finish. Said now
			 * rather than after a pointless wait. */
			info->last_network_status = MATTER_NC_STATUS_OTHER_CONNECTION_FAILUR;
			return MATTER_IM_STATUS_SUCCESS;
		}
		/*
		 * Blocks. The commissioner is waiting on this reply and allows
		 * up to the ConnectMaxTimeSeconds this node advertises, so the
		 * bound below has to stay comfortably under it. Reporting
		 * Success before the node is actually on the network would send
		 * the commissioner hunting for it and cost far more than a wait.
		 */
		if (matter_thread_wait_attached(MATTER_THREAD_ATTACH_TIMEOUT_MS) != MATTER_OK) {
			info->last_network_status = MATTER_NC_STATUS_OTHER_CONNECTION_FAILUR;
			return MATTER_IM_STATUS_SUCCESS;
		}
		/*
		 * On the network, and now findable ON it. The commissioner
		 * closes BLE the moment this reply says Success and looks the
		 * node up in DNS-SD; registering after that would be a race
		 * against a search already under way.
		 */
		advertise_operational(info);
		info->last_network_status = MATTER_NC_STATUS_SUCCESS;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_NC_REMOVE_NETWORK:
		*response_command = MATTER_CMD_NC_NETWORK_CONFIG_RESPONSE;
		if (!info->have_thread_xpanid) {
			info->last_network_status = MATTER_NC_STATUS_NETWORK_ID_NOT_FOUND;
			return MATTER_IM_STATUS_SUCCESS;
		}
		info->thread_dataset_len = 0u;
		info->have_thread_xpanid = false;
		info->last_network_status = MATTER_NC_STATUS_SUCCESS;
		return MATTER_IM_STATUS_SUCCESS;

	default:
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
}

/** Serialise what network_command() decided. */
static void network_fields(const struct matter_device_info *info, uint32_t response_command,
			   struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);

	if (response_command == MATTER_CMD_NC_CONNECT_NETWORK_RESPONSE) {
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CONNRESP_STATUS),
					 info->last_network_status);
		/* ErrorValue is nullable and mandatory: null is what a device
		 * sends when the failure has no driver-specific code behind it. */
		(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_CONNRESP_ERROR));
	} else {
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NCRESP_STATUS),
					 info->last_network_status);
		if (info->last_network_status == MATTER_NC_STATUS_SUCCESS) {
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NCRESP_INDEX), 0u);
		}
	}

	(void)matter_tlv_end_container(w);
}

/**
 * Install the root the commissioner wants this node to trust.
 *
 * Only the public key is kept -- see matter_fabric.h. Nothing is verified: this
 * node has no prior opinion about which roots are legitimate, which is exactly
 * what makes it commissionable.
 */
static uint8_t add_trusted_root(struct matter_device_info *info, const struct matter_im_invoke *inv)
{
	const uint8_t *cert = NULL;
	size_t cert_len = 0u;
	struct matter_cert_info ci;

	if (!field_bytes(inv, TAG_ADDROOT_CERT, &cert, &cert_len) || cert_len > MATTER_CERT_MAX) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}
	if (matter_cert_parse(cert, cert_len, &ci) != MATTER_OK || !ci.have_public_key) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}

	{
		struct matter_fabric *f = fabric_pending(info);

		/* Every slot already holds a complete fabric. Refused here
		 * rather than at AddNOC, so the commissioner learns before it
		 * mints an operational certificate this node cannot accept. */
		if (f == NULL) {
			return MATTER_IM_STATUS_RESOURCE_EXHAUSTED;
		}
		memcpy(f->root_public_key, ci.public_key, sizeof(ci.public_key));
		f->have_root = true;
	}
	return MATTER_IM_STATUS_SUCCESS;
}

/**
 * Accept the operational identity the commissioner minted for this node.
 *
 * @return the NodeOperationalCertStatusEnum for the reply. Every refusal is one
 *         of these rather than an IM status, because each names WHICH input was
 *         wrong and a commissioner can act on that.
 */
static uint8_t add_noc(struct matter_device_info *info, const struct matter_im_invoke *inv)
{
	const uint8_t *noc = NULL;
	const uint8_t *icac = NULL;
	const uint8_t *ipk = NULL;
	size_t noc_len = 0u;
	size_t icac_len = 0u;
	size_t ipk_len = 0u;
	struct matter_cert_info ci;
	struct matter_fabric *fab;
	uint64_t v = 0u;

	if (!info->have_op_key) {
		/* No CSR, so there is no private key behind whatever public key
		 * this NOC certifies. */
		return MATTER_NOC_STATUS_MISSING_CSR;
	}
	fab = fabric_pending(info);
	if (fab == NULL) {
		/* Every slot holds a complete fabric already. This is what a
		 * second administrator sees on a node built for one, and it is
		 * where Apple stopped: the phone and the home hub each want
		 * their own. */
		return MATTER_NOC_STATUS_TABLE_FULL;
	}
	if (!fab->have_root) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}

	if (!field_bytes(inv, TAG_ADDNOC_NOC, &noc, &noc_len) || noc_len > MATTER_NOC_MAX ||
	    !field_bytes(inv, TAG_ADDNOC_IPK, &ipk, &ipk_len) || ipk_len != MATTER_IPK_LEN) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}
	if (matter_cert_parse(noc, noc_len, &ci) != MATTER_OK || !ci.have_node_id ||
	    !ci.have_fabric_id || !ci.have_public_key) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}
	/*
	 * The certified key must be the one this node minted for the CSR.
	 * Installing an identity whose private half this node does not hold
	 * would look like success here and surface much later as a CASE that
	 * never completes, with nothing to point at.
	 */
	if (memcmp(ci.public_key, info->op_pub, sizeof(info->op_pub)) != 0) {
		return MATTER_NOC_STATUS_INVALID_PUBLIC_KEY;
	}
	/* Optional: absent when the commissioner signed the NOC with its root
	 * directly, which is what Apple does. */
	(void)field_bytes(inv, TAG_ADDNOC_ICAC, &icac, &icac_len);
	if (icac_len > MATTER_CERT_MAX) {
		return MATTER_NOC_STATUS_INVALID_NOC;
	}

	memcpy(fab->noc, noc, noc_len);
	fab->noc_len = noc_len;
	if (icac_len != 0u) {
		memcpy(info->icac.buf, icac, icac_len);
		info->icac.len = icac_len;
		info->icac.owner_index = fabric_next_index(info);
	}
	fab->icac_len = icac_len;
	memcpy(fab->ipk, ipk, ipk_len);
	/*
	 * The key this fabric's NOC certifies, taken from the CSR that preceded
	 * it. Copied INTO the fabric rather than left in info->op_priv: the next
	 * administrator issues its own CSRRequest and overwrites that, and a
	 * node that signed Sigma2 with the wrong fabric's key would verify
	 * against the wrong certificate and be refused with nothing said.
	 */
	memcpy(fab->op_priv, info->op_priv, sizeof(fab->op_priv));
	fab->node_id = ci.node_id;
	fab->fabric_id = ci.fabric_id;
	if (field_u64(inv, TAG_ADDNOC_CASE_ADMIN_SUBJECT, &v)) {
		fab->case_admin_subject = v;
	}
	if (field_u64(inv, TAG_ADDNOC_ADMIN_VENDOR_ID, &v) && v <= UINT16_MAX) {
		fab->admin_vendor_id = (uint16_t)v;
	}
	fab->index = fabric_next_index(info);
	/*
	 * Findable on the new fabric, immediately. A second administrator adds
	 * its fabric over an EXISTING CASE session and then has to open a new
	 * one to the identity it has just issued -- which means resolving a
	 * DNS-SD name that only exists once this runs. Registration at
	 * ConnectNetwork covers the first fabric only; that is where the second
	 * administrator stopped, with the fabric accepted and nothing to reach.
	 */
	advertise_operational(info);
	/* Held for the response, which is serialised after this has run. */
	info->last_noc_index = fab->index;
	return MATTER_NOC_STATUS_OK;
}

/**
 * Run one OperationalCredentials command.
 *
 * Everything expensive happens here -- the signature, and for a CSR a fresh
 * P-256 key pair -- because this runs exactly once per request while
 * opcred_fields() may not.
 */
static uint8_t opcred_command(struct matter_device_info *info, const struct matter_im_invoke *inv,
			      uint32_t *response_command)
{
	const uint8_t *nonce = NULL;
	size_t nonce_len = 0u;
	uint64_t v = 0u;
	int rc;

	switch (inv->command) {
	case MATTER_CMD_OC_CERTIFICATE_CHAIN_REQUEST: {
		const uint8_t *cert = NULL;
		size_t cert_len = 0u;

		if (!field_u64(inv, TAG_CERT_TYPE, &v) || v > UINT8_MAX) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/* Checked here rather than when writing the reply, because a
		 * reply has no way to say "no such certificate". */
		if (matter_attest_cert((uint8_t)v, &cert, &cert_len) != MATTER_OK) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		info->cert_type = (uint8_t)v;
		*response_command = MATTER_CMD_OC_CERTIFICATE_CHAIN_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;
	}

	case MATTER_CMD_OC_ATTESTATION_REQUEST:
		if (!info->have_challenge) {
			/* Nothing to bind the signature to. Refusing beats
			 * signing something a recorded session could reuse. */
			return MATTER_IM_STATUS_FAILURE;
		}
		if (!field_bytes(inv, TAG_ATTEST_NONCE, &nonce, &nonce_len) ||
		    nonce_len != MATTER_ATTEST_NONCE_LEN) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/* No clock on this node, and 0 is what a device without one
		 * sends -- not a placeholder for something better. */
		rc = matter_attest_elements_encode(nonce, nonce_len, 0u, info->attest_buf,
						   MATTER_ATTEST_ELEMENTS_MAX, &info->attest_len);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_sign_with_challenge(
			info->attest_buf, info->attest_len, sizeof(info->attest_buf),
			info->attestation_challenge, sizeof(info->attestation_challenge),
			info->attest_sig);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		*response_command = MATTER_CMD_OC_ATTESTATION_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_OC_CSR_REQUEST: {
		uint8_t csr[MATTER_CSR_MAX];
		size_t csr_len = 0u;

		if (!info->have_challenge) {
			return MATTER_IM_STATUS_FAILURE;
		}
		if (!field_bytes(inv, TAG_CSR_NONCE, &nonce, &nonce_len) ||
		    nonce_len != MATTER_ATTEST_NONCE_LEN) {
			return MATTER_IM_STATUS_INVALID_COMMAND;
		}
		/*
		 * A FRESH key every time. Reusing one across commissioning
		 * attempts would let a fabric that saw an earlier CSR recognise
		 * the node on another.
		 */
		if (matter_attest_ec_keygen(info->op_priv, info->op_pub) != 0) {
			return MATTER_IM_STATUS_FAILURE;
		}
		info->have_op_key = true;
		if (matter_attest_csr(info->op_priv, info->op_pub, csr, sizeof(csr), &csr_len) !=
		    MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_nocsr_encode(csr, csr_len, nonce, nonce_len, info->attest_buf,
						MATTER_ATTEST_ELEMENTS_MAX, &info->attest_len);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		rc = matter_attest_sign_with_challenge(
			info->attest_buf, info->attest_len, sizeof(info->attest_buf),
			info->attestation_challenge, sizeof(info->attestation_challenge),
			info->attest_sig);
		if (rc != MATTER_OK) {
			return MATTER_IM_STATUS_FAILURE;
		}
		*response_command = MATTER_CMD_OC_CSR_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;
	}

	case MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE:
		/*
		 * Both of the commands below change what this node believes
		 * about who owns it, which is precisely what a fail-safe exists
		 * to be able to undo. Doing either outside one would leave a
		 * half-installed identity with nothing scheduled to remove it.
		 */
		if (!info->failsafe_armed) {
			return MATTER_IM_STATUS_FAILSAFE_REQUIRED;
		}
		/* No response command: the reply is a bare SUCCESS status. */
		*response_command = MATTER_IM_NO_RESPONSE;
		return add_trusted_root(info, inv);

	case MATTER_CMD_OC_ADD_NOC:
		if (!info->failsafe_armed) {
			return MATTER_IM_STATUS_FAILSAFE_REQUIRED;
		}
		info->last_noc_status = add_noc(info, inv);
		*response_command = MATTER_CMD_OC_NOC_RESPONSE;
		/* SUCCESS means "a NOCResponse follows", not "the NOC was
		 * accepted"; last_noc_status carries the verdict. */
		return MATTER_IM_STATUS_SUCCESS;

	default:
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
}

/** Serialise what opcred_command() already computed. */
static void opcred_fields(const struct matter_device_info *info, uint32_t response_command,
			  struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);

	if (response_command == MATTER_CMD_OC_CERTIFICATE_CHAIN_RESPONSE) {
		const uint8_t *cert = NULL;
		size_t cert_len = 0u;

		if (matter_attest_cert(info->cert_type, &cert, &cert_len) == MATTER_OK) {
			(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_CERT), cert,
						   cert_len);
		}
	} else if (response_command == MATTER_CMD_OC_NOC_RESPONSE) {
		(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NOCRESP_STATUS),
					 info->last_noc_status);
		/*
		 * FabricIndex only on success, and DebugText not at all. Both
		 * are optional and CHIP's own device omits them the same way
		 * (operational-credentials-cluster.cpp, SendNOCResponse) -- an
		 * index for a fabric that was not created would be a number the
		 * commissioner could act on.
		 */
		if (info->last_noc_status == MATTER_NOC_STATUS_OK) {
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_NOCRESP_FABRIC_INDEX),
						 info->last_noc_index);
		}
	} else {
		/* AttestationResponse and CSRResponse are the same shape: the
		 * elements, then the signature over them. */
		(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_ELEMENTS), info->attest_buf,
					   info->attest_len);
		(void)matter_tlv_put_bytes(w, MATTER_TLV_CTX(TAG_RESP_SIGNATURE), info->attest_sig,
					   sizeof(info->attest_sig));
	}

	(void)matter_tlv_end_container(w);
}

/**
 * SetAliroReaderConfig: the reader identity, delivered by Apple Home.
 *
 * This is the point of the whole Matter node. Until now the reader's private
 * key was a build-time Kconfig string, so every image carried one identity and
 * only unlocked for the phones in whoever built it. This command is how a
 * device gets its own.
 *
 * NOTHING HERE IS LOGGED. Every field is key material, and the signing key is
 * not even kept in this struct: it goes straight to the port's callback, which
 * persists it, and is wiped from the stack on the way out.
 *
 * The group resolving key is optional in the schema but not here: this node
 * claims the BLE-UWB feature, and that is exactly the bit that makes it
 * mandatory. Accepting a config without it would leave the reader unable to
 * resolve the group it was just told it belongs to.
 */
static uint8_t set_aliro_reader_config(struct matter_device_info *info,
				       const struct matter_im_invoke *inv)
{
	const uint8_t *signing = NULL;
	const uint8_t *verification = NULL;
	const uint8_t *group_id = NULL;
	const uint8_t *grk = NULL;
	size_t signing_len = 0u;
	size_t verification_len = 0u;
	size_t group_id_len = 0u;
	size_t grk_len = 0u;

	if (!field_bytes(inv, TAG_ALIRO_CFG_SIGNING_KEY, &signing, &signing_len) ||
	    !field_bytes(inv, TAG_ALIRO_CFG_VERIFICATION_KEY, &verification, &verification_len) ||
	    !field_bytes(inv, TAG_ALIRO_CFG_GROUP_ID, &group_id, &group_id_len)) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}
	/*
	 * Lengths are checked before anything is copied, and checked exactly:
	 * a 64-byte "P-256 public key" is a different thing from a 65-byte one
	 * and would be stored happily by a length-tolerant reader.
	 */
	if (signing_len != MATTER_ALIRO_SIGNING_KEY_LEN ||
	    verification_len != MATTER_ALIRO_VERIFICATION_KEY_LEN ||
	    group_id_len != MATTER_ALIRO_GROUP_ID_LEN) {
		return MATTER_IM_STATUS_CONSTRAINT_ERROR;
	}
	if (!field_bytes(inv, TAG_ALIRO_CFG_GROUP_RESOLVING_KEY, &grk, &grk_len) ||
	    grk_len != MATTER_ALIRO_GROUP_ID_LEN) {
		return MATTER_IM_STATUS_CONSTRAINT_ERROR;
	}

	if (info->aliro_reader_config_cb == NULL) {
		/* No store to write to. Reporting success would claim an
		 * identity was kept that will be gone at the next boot. */
		return MATTER_IM_STATUS_FAILURE;
	}
	if (info->aliro_reader_config_cb(signing, verification, group_id, grk) != 0) {
		return MATTER_IM_STATUS_FAILURE;
	}

	memcpy(info->aliro_verification_key, verification, MATTER_ALIRO_VERIFICATION_KEY_LEN);
	memcpy(info->aliro_group_id, group_id, MATTER_ALIRO_GROUP_ID_LEN);
	memcpy(info->aliro_group_resolving_key, grk, MATTER_ALIRO_GROUP_ID_LEN);
	info->have_aliro_group_resolving_key = true;
	info->have_aliro_reader_config = true;
	return MATTER_IM_STATUS_SUCCESS;
}

static uint8_t command(void *ctx, const struct matter_im_invoke *inv, uint32_t *response_command)
{
	struct matter_device_info *info = (struct matter_device_info *)ctx;
	uint64_t v = 0u;

	/*
	 * The lock endpoint answers commands too. Refusing every command here
	 * with UNSUPPORTED_ENDPOINT while Descriptor, PartsList and every
	 * attribute read say endpoint 1 exists is a direct self-contradiction,
	 * and it is what a real controller reported back as "the endpoint
	 * indicated is unsupported on the node" before abandoning the pairing.
	 *
	 * UNSUPPORTED_COMMAND is the honest answer for the Door Lock commands
	 * this node has not implemented: the endpoint and the cluster are both
	 * real, the command is not. A controller can act on that; it cannot act
	 * on an endpoint that claims to exist and not exist at once.
	 */
	if (inv->endpoint == MATTER_ENDPOINT_LOCK) {
		if (inv->cluster != MATTER_CLUSTER_DOOR_LOCK) {
			return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
		}
		if (inv->command == MATTER_CMD_DL_SET_ALIRO_READER_CONFIG) {
			return set_aliro_reader_config(info, inv);
		}
		if (inv->command == MATTER_CMD_DL_GET_USER) {
			/*
			 * Answered because a real controller invokes it during
			 * commissioning and gives up when it is refused -- it
			 * reported "the specified action or command indicated
			 * is not supported" and sent RemoveFabric.
			 *
			 * The answer is an EMPTY slot, which is the truth:
			 * there is no user database here yet. UserIndex is
			 * echoed and every other field is null, which is how
			 * the spec says an unoccupied slot reads.
			 */
			if (!field_u64(inv, TAG_GETUSER_INDEX, &v) || v == 0u ||
			    v > MATTER_DL_USERS_MAX) {
				return MATTER_IM_STATUS_INVALID_COMMAND;
			}
			info->last_user_index = (uint16_t)v;
			*response_command = MATTER_CMD_DL_GET_USER_RESPONSE;
			return MATTER_IM_STATUS_SUCCESS;
		}
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
	if (inv->endpoint != MATTER_ENDPOINT_ROOT) {
		return MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT;
	}
	if (inv->cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		return opcred_command(info, inv, response_command);
	}
	if (inv->cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING) {
		return network_command(info, inv, response_command);
	}
	if (inv->cluster != MATTER_CLUSTER_GENERAL_COMMISSIONING) {
		return MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
	}

	switch (inv->command) {
	case MATTER_CMD_GC_ARM_FAIL_SAFE:
		/*
		 * The breadcrumb is the commissioner's own progress marker: it
		 * sets it here and reads it back if it has to resume, so losing
		 * it makes a retry restart from nothing.
		 */
		if (field_u64(inv, 1u, &v)) {
			info->breadcrumb = v;
		}
		info->failsafe_armed = true;
		info->last_commissioning_error = MATTER_COMMISSIONING_OK;
		*response_command = MATTER_CMD_GC_ARM_FAIL_SAFE_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_GC_SET_REGULATORY_CONFIG:
		if (field_u64(inv, 0u, &v)) {
			/* Accept only what LocationCapability claims to support.
			 * Saying yes to a location this node cannot honour is a
			 * lie the commissioner has no way to detect. */
			if (v != info->location_capability &&
			    info->location_capability != MATTER_REGULATORY_INDOOR_OUTDOOR) {
				info->last_commissioning_error =
					MATTER_COMMISSIONING_VALUE_OUTSIDE_RANGE;
				*response_command = MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE;
				return MATTER_IM_STATUS_SUCCESS;
			}
			info->regulatory_config = (uint8_t)v;
		}
		if (field_u64(inv, 2u, &v)) {
			info->breadcrumb = v;
		}
		info->last_commissioning_error = MATTER_COMMISSIONING_OK;
		*response_command = MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE;
		return MATTER_IM_STATUS_SUCCESS;

	case MATTER_CMD_GC_COMMISSIONING_COMPLETE:
		*response_command = MATTER_CMD_GC_COMMISSIONING_COMPLETE_RESPONSE;
		/*
		 * Refused over anything but CASE, and NO_FAIL_SAFE rather than
		 * OK because OK would be a lie: the command asserts that the
		 * commissioner has reached this node operationally, and until a
		 * CASE session exists it has not. Telling the truth makes the
		 * commissioner fail cleanly instead of believing it owns a node
		 * it cannot reach.
		 */
		if (!info->case_established) {
			info->last_commissioning_error = MATTER_COMMISSIONING_NO_FAIL_SAFE;
			return MATTER_IM_STATUS_SUCCESS;
		}
		/*
		 * Commit. Disarming the fail-safe is the substance of the
		 * command, not bookkeeping: until this point every fabric,
		 * every key and the whole operational identity are provisional
		 * and matter_clusters_failsafe_expire() will erase them.
		 */
		info->commissioning_complete = true;
		info->failsafe_armed = false;
		info->last_commissioning_error = MATTER_COMMISSIONING_OK;
		return MATTER_IM_STATUS_SUCCESS;

	default:
		return MATTER_IM_STATUS_UNSUPPORTED_COMMAND;
	}
}

static void command_fields(void *ctx, uint16_t endpoint, uint32_t cluster,
			   uint32_t response_command, struct matter_tlv_writer *w,
			   matter_tlv_tag_t tag)
{
	const struct matter_device_info *info = (const struct matter_device_info *)ctx;

	if (endpoint == MATTER_ENDPOINT_LOCK) {
		if (cluster == MATTER_CLUSTER_DOOR_LOCK &&
		    response_command == MATTER_CMD_DL_GET_USER_RESPONSE) {
			/*
			 * An unoccupied slot: the index that was asked for, and
			 * null for everything that describes a user who is not
			 * there. NextUserIndex is null too -- there is no next
			 * occupied slot to walk to.
			 *
			 * The CommandFields STRUCTURE is opened here, by the
			 * callee, exactly as opcred_fields and network_fields
			 * do. Writing the fields bare puts them in the
			 * CommandDataIB beside the path instead of inside it,
			 * and the result is a response that encodes without
			 * error, decodes as garbage, and is simply dropped.
			 */
			(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
			(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_GETUSER_INDEX),
						 info->last_user_index);
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_NAME));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_UNIQUE_ID));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_STATUS));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_TYPE));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_CREDENTIAL_RULE));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_CREDENTIALS));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_CREATOR_FABRIC));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_MODIFIER_FABRIC));
			(void)matter_tlv_put_null(w, MATTER_TLV_CTX(TAG_GETUSER_NEXT_INDEX));
			(void)matter_tlv_end_container(w);
		}
		return;
	}
	(void)endpoint;

	if (cluster == MATTER_CLUSTER_OPERATIONAL_CREDENTIALS) {
		opcred_fields(info, response_command, w, tag);
		return;
	}
	if (cluster == MATTER_CLUSTER_NETWORK_COMMISSIONING) {
		network_fields(info, response_command, w, tag);
		return;
	}
	(void)response_command;

	/*
	 * All three GeneralCommissioning responses carry the same two fields:
	 * ErrorCode then DebugText (Commands.h:133-134, 209-210). DebugText is
	 * mandatory and empty, not omitted -- a missing mandatory field is a
	 * decode failure at the commissioner, which presents as a hang.
	 */
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(0u), info->last_commissioning_error);
	(void)matter_tlv_put_utf8(w, MATTER_TLV_CTX(1u), "", 0u);
	(void)matter_tlv_end_container(w);
}

void matter_clusters_failsafe_expire(struct matter_device_info *info)
{
	if (info == NULL || info->commissioning_complete) {
		return;
	}

	/*
	 * Every fabric, and the keys their NOCs certify. Wiped rather than
	 * cleared by index alone: each holds a private key that outlived the
	 * only thing that was going to use it.
	 *
	 * All of them, because the fail-safe covers the whole window and a
	 * commissioner that abandoned it mid-way may have created more than one.
	 */
	memset(info->fabrics, 0, sizeof(info->fabrics));
	memset(&info->icac, 0, sizeof(info->icac));
	memset(info->op_priv, 0, sizeof(info->op_priv));
	memset(info->op_pub, 0, sizeof(info->op_pub));
	info->have_op_key = false;
	info->last_noc_status = MATTER_NOC_STATUS_OK;

	/* Not the dataset or the attachment -- see matter_clusters.h. */
	info->failsafe_armed = false;
	info->breadcrumb = 0u;
	info->last_commissioning_error = MATTER_COMMISSIONING_OK;
}

/**
 * Apply an attribute write.
 *
 * One attribute is writable on this node: the ACL. A commissioner's last act is
 * writing itself an entry granting Administer over CASE, and refusing it leaves
 * a home app that finished commissioning and then cannot record that it owns
 * the node -- which is what "Adding to home" is waiting on.
 *
 * The value is stored as the TLV that arrived. Nothing decodes it and nothing
 * consults it; see the note on matter_device_info.acl.
 */
static uint8_t attr_write(void *ctx, const struct matter_im_path *path, const uint8_t *data,
			  size_t data_len)
{
	struct matter_device_info *info = (struct matter_device_info *)ctx;

	if (info == NULL || path->endpoint != MATTER_ENDPOINT_ROOT) {
		return MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT;
	}
	if (path->cluster != MATTER_CLUSTER_ACCESS_CONTROL) {
		/* Every other cluster this node has is read-only, so the honest
		 * answer distinguishes "no such cluster" from "not writable". */
		return has_cluster(ctx, path->endpoint, path->cluster)
			       ? MATTER_IM_STATUS_UNSUPPORTED_WRITE
			       : MATTER_IM_STATUS_UNSUPPORTED_CLUSTER;
	}
	if (path->attribute != MATTER_ATTR_AC_ACL) {
		return MATTER_IM_STATUS_UNSUPPORTED_WRITE;
	}
	if (data == NULL || data_len == 0u) {
		return MATTER_IM_STATUS_INVALID_COMMAND;
	}
	/*
	 * Refused rather than truncated. A half-stored ACL would be read back as
	 * a shorter list than the commissioner wrote, and it would look like the
	 * node had silently dropped entries it was asked to grant.
	 */
	if (data_len > sizeof(info->acl)) {
		return MATTER_IM_STATUS_RESOURCE_EXHAUSTED;
	}

	memcpy(info->acl, data, data_len);
	info->acl_len = data_len;
	return MATTER_IM_STATUS_SUCCESS;
}

void matter_clusters_init(struct matter_im_server *srv, struct matter_device_info *info)
{
	if (srv == NULL) {
		return;
	}
	srv->status = attr_status;
	srv->value = attr_value;
	srv->has_cluster = has_cluster;
	srv->list_attrs = list_attrs;
	srv->list_endpoints = list_endpoints;
	srv->list_clusters = list_clusters;
	srv->command = command;
	srv->command_fields = command_fields;
	srv->write = attr_write;
	srv->ctx = info;
}
