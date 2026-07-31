/**
 * @file matter_clusters.h — what this device answers, as opposed to how.
 *
 * matter_im.c owns the ReportData wire format and knows nothing about door
 * locks or vendor IDs. This is the other half: the endpoints, clusters and
 * attributes that exist, and what they say.
 *
 * Scope is deliberately the commissioner's FIRST question and no further. A
 * real iPhone, immediately after PASE, reads nine attribute paths:
 *
 *   endpoint 0  GeneralCommissioning 0x0030  attributes 0x00..0x04 and 0x0C
 *   endpoint 0  BasicInformation     0x0028  VendorID 0x02, ProductID 0x04
 *   endpoint 0  TimeSynchronization  0x0038  all attributes (wildcard)
 *
 * Everything else answers UNSUPPORTED_*, which is a legal answer and a truthful
 * one. Clusters get added when a commissioner is observed asking for them,
 * rather than because the spec lists them.
 *
 * Device-specific values arrive in @ref matter_device_info instead of being
 * read from Kconfig here, so the host suite can build this without Zephyr and
 * assert on the encoded bytes.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Cluster and attribute IDs transcribed from
 * workspace/modules/lib/matter/zzz_generated/app-common/clusters/, cited at
 * each definition below.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matter_im.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cluster IDs, each from that cluster's generated ClusterId.h:14. */
#define MATTER_CLUSTER_BASIC_INFORMATION     0x0028u
#define MATTER_CLUSTER_GENERAL_COMMISSIONING 0x0030u

/* BasicInformation attributes (BasicInformation/AttributeIds.h:27,35). */
#define MATTER_ATTR_BASIC_VENDOR_ID  0x0002u
#define MATTER_ATTR_BASIC_PRODUCT_ID 0x0004u

/* GeneralCommissioning attributes (GeneralCommissioning/AttributeIds.h:19-36). */
#define MATTER_ATTR_GC_BREADCRUMB                     0x0000u
#define MATTER_ATTR_GC_BASIC_COMMISSIONING_INFO       0x0001u
#define MATTER_ATTR_GC_REGULATORY_CONFIG              0x0002u
#define MATTER_ATTR_GC_LOCATION_CAPABILITY            0x0003u
#define MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION 0x0004u

/* RegulatoryLocationTypeEnum (GeneralCommissioning/Enums.h:65-67). */
#define MATTER_REGULATORY_INDOOR         0u
#define MATTER_REGULATORY_OUTDOOR        1u
#define MATTER_REGULATORY_INDOOR_OUTDOOR 2u

/* GeneralCommissioning commands (GeneralCommissioning/CommandIds.h:21-46). */
#define MATTER_CMD_GC_ARM_FAIL_SAFE                   0x0000u
#define MATTER_CMD_GC_ARM_FAIL_SAFE_RESPONSE          0x0001u
#define MATTER_CMD_GC_SET_REGULATORY_CONFIG           0x0002u
#define MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE  0x0003u
#define MATTER_CMD_GC_COMMISSIONING_COMPLETE          0x0004u
#define MATTER_CMD_GC_COMMISSIONING_COMPLETE_RESPONSE 0x0005u

/* CommissioningErrorEnum (GeneralCommissioning/Enums.h:34-41). */
#define MATTER_COMMISSIONING_OK                  0u
#define MATTER_COMMISSIONING_VALUE_OUTSIDE_RANGE 1u
#define MATTER_COMMISSIONING_INVALID_AUTH        2u
#define MATTER_COMMISSIONING_NO_FAIL_SAFE        3u

/** The only endpoint this node has. Every Matter node's root endpoint is 0. */
#define MATTER_ENDPOINT_ROOT 0u

struct matter_device_info {
	uint16_t vendor_id;
	uint16_t product_id;
	/**
	 * Written by the commissioner through GeneralCommissioning so it can
	 * tell how far a previous attempt got. Mutable, and the reason this
	 * struct is not const.
	 */
	uint64_t breadcrumb;
	uint8_t regulatory_config;
	uint8_t location_capability;
	/** BasicCommissioningInfo, seconds (GeneralCommissioning/Structs.h:50-51). */
	uint16_t failsafe_expiry_s;
	uint16_t failsafe_max_s;
	/**
	 * Whether BLE stays up while the node joins its operational network.
	 *
	 * False tells the commissioner to expect this node to vanish from BLE
	 * and reappear on Thread. This node has no operational network at all
	 * yet, so false would promise a reappearance that never comes.
	 */
	bool supports_concurrent_connection;
	/**
	 * True once ArmFailSafe has been accepted and not yet expired.
	 *
	 * No timer behind it yet. Matter says the fail-safe disarms itself after
	 * ExpiryLengthSeconds and undoes whatever the half-finished commissioning
	 * changed; nothing here changes anything that would need undoing, so the
	 * flag records the state without pretending to enforce it. That becomes a
	 * real timer when there is provisioning to roll back.
	 */
	bool failsafe_armed;
	/**
	 * The CommissioningErrorEnum the last GeneralCommissioning command
	 * produced, held between running it and serialising its response --
	 * matter_im_command_fields_fn is pure and cannot recompute it.
	 */
	uint8_t last_commissioning_error;
};

/**
 * Point @p srv at this data model.
 *
 * @param info borrowed, not copied, and must outlive @p srv. Breadcrumb is
 *        written through it.
 */
void matter_clusters_init(struct matter_im_server *srv, struct matter_device_info *info);

#ifdef __cplusplus
}
#endif
