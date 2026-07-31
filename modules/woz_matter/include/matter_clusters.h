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

#include "matter_attest.h"
#include "matter_fabric.h"
#include "matter_im.h"
#include "matter_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cluster IDs, each from that cluster's generated ClusterId.h:14. */
#define MATTER_CLUSTER_BASIC_INFORMATION       0x0028u
#define MATTER_CLUSTER_GENERAL_COMMISSIONING   0x0030u
#define MATTER_CLUSTER_NETWORK_COMMISSIONING   0x0031u
#define MATTER_CLUSTER_OPERATIONAL_CREDENTIALS 0x003Eu

/** FeatureMap, on every cluster (GlobalAttributeIds.h). */
#define MATTER_ATTR_FEATURE_MAP 0xFFFCu

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

/* NetworkCommissioning attributes (MTRClusterConstants.h:1165-1169). */
#define MATTER_ATTR_NC_MAX_NETWORKS           0x0000u
#define MATTER_ATTR_NC_NETWORKS               0x0001u
#define MATTER_ATTR_NC_SCAN_MAX_TIME_S        0x0002u
#define MATTER_ATTR_NC_CONNECT_MAX_TIME_S     0x0003u
#define MATTER_ATTR_NC_INTERFACE_ENABLED      0x0004u
#define MATTER_ATTR_NC_LAST_NETWORKING_STATUS 0x0005u

/* NetworkCommissioning commands (MTRClusterConstants.h:6296-6300). */
#define MATTER_CMD_NC_ADD_OR_UPDATE_THREAD_NETWORK 0x0003u
#define MATTER_CMD_NC_REMOVE_NETWORK               0x0004u
#define MATTER_CMD_NC_NETWORK_CONFIG_RESPONSE      0x0005u
#define MATTER_CMD_NC_CONNECT_NETWORK              0x0006u
#define MATTER_CMD_NC_CONNECT_NETWORK_RESPONSE     0x0007u

/** NetworkCommissioning Feature bits (python clusters/Objects.py, Feature). */
#define MATTER_NC_FEATURE_THREAD 0x2u

/* NetworkCommissioningStatusEnum, the values this node returns. */
#define MATTER_NC_STATUS_SUCCESS                 0x00u
#define MATTER_NC_STATUS_OUT_OF_RANGE            0x01u
#define MATTER_NC_STATUS_NETWORK_ID_NOT_FOUND    0x03u
#define MATTER_NC_STATUS_OTHER_CONNECTION_FAILUR 0x09u

/**
 * A Thread operational dataset, kSizeOperationalDataset
 * (lib/support/ThreadOperationalDataset.h:36).
 */
#define MATTER_THREAD_DATASET_MAX 254u

/** Extended PAN ID: 8 bytes, and the id a network is referred to by. */
#define MATTER_THREAD_XPANID_LEN 8u

/**
 * How long ConnectNetwork waits for the attach before answering.
 *
 * Well under the 60 s this node reports as ConnectMaxTimeSeconds, because the
 * commissioner is blocked on the reply for the whole of it. A Thread attach to
 * a network whose dataset is already known normally completes in a few seconds;
 * this leaves room for a retry without leaving the phone waiting a minute.
 */
#define MATTER_THREAD_ATTACH_TIMEOUT_MS 20000u

/* OperationalCredentials commands (MTRClusterConstants.h:6435-6446). */
#define MATTER_CMD_OC_ATTESTATION_REQUEST          0x0000u
#define MATTER_CMD_OC_ATTESTATION_RESPONSE         0x0001u
#define MATTER_CMD_OC_CERTIFICATE_CHAIN_REQUEST    0x0002u
#define MATTER_CMD_OC_CERTIFICATE_CHAIN_RESPONSE   0x0003u
#define MATTER_CMD_OC_CSR_REQUEST                  0x0004u
#define MATTER_CMD_OC_CSR_RESPONSE                 0x0005u
#define MATTER_CMD_OC_ADD_NOC                      0x0006u
#define MATTER_CMD_OC_NOC_RESPONSE                 0x0008u
#define MATTER_CMD_OC_ADD_TRUSTED_ROOT_CERTIFICATE 0x000Bu

/* OperationalCredentials attributes (MTRClusterConstants.h:1988-1989). */
#define MATTER_ATTR_OC_SUPPORTED_FABRICS    0x0002u
#define MATTER_ATTR_OC_COMMISSIONED_FABRICS 0x0003u

/**
 * How many fabrics this node can hold at once.
 *
 * One. The spec's floor is five, and a node meant for several homes needs the
 * table; this one exists to be provisioned by a single Apple Home and there is
 * a struct matter_fabric of RAM behind each entry.
 */
#define MATTER_SUPPORTED_FABRICS 1u

/*
 * NodeOperationalCertStatusEnum, the verdicts this node actually returns
 * (OperationalCredentials Enums, python clusters/Objects.py). AddNOC reports
 * failure THROUGH this enum rather than through an IM status: the commissioner
 * has to be told which of its inputs was rejected, and a bare FAILURE cannot
 * say.
 */
#define MATTER_NOC_STATUS_OK                 0u
#define MATTER_NOC_STATUS_INVALID_PUBLIC_KEY 1u
#define MATTER_NOC_STATUS_INVALID_NOC        3u
#define MATTER_NOC_STATUS_MISSING_CSR        4u
#define MATTER_NOC_STATUS_TABLE_FULL         5u

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

	/*
	 * ---- attestation --------------------------------------------------
	 *
	 * A command runs once and its reply is serialised afterwards, so
	 * whatever the reply needs is recorded here in between. All of it is
	 * per-session and none of it survives a new commissioner.
	 */

	/**
	 * The session's attestation challenge, which every attestation
	 * signature covers. Copied from the PASE keys when the secure session
	 * comes up; without it a recorded response could be replayed into a
	 * different session.
	 */
	uint8_t attestation_challenge[MATTER_ATTEST_CHALLENGE_LEN];
	bool have_challenge;

	/**
	 * Scratch for the reply, with the headroom
	 * matter_attest_sign_with_challenge() writes the challenge into.
	 */
	uint8_t attest_buf[MATTER_ATTEST_ELEMENTS_MAX + MATTER_ATTEST_CHALLENGE_LEN];
	size_t attest_len;
	uint8_t attest_sig[MATTER_ATTEST_SIG_LEN];
	/** Which certificate CertificateChainResponse should carry. */
	uint8_t cert_type;

	/**
	 * The operational key pair, minted when the commissioner asks for a
	 * CSR. The private half never leaves this node -- that is the point of
	 * a CSR -- and the commissioner certifies the public half into a NOC.
	 */
	uint8_t op_priv[32];
	uint8_t op_pub[65];
	bool have_op_key;

	/*
	 * ---- operational identity ------------------------------------------
	 */

	/** The one fabric this node can hold. Empty until AddNOC succeeds. */
	struct matter_fabric fabric;

	/*
	 * ---- the operational network ---------------------------------------
	 *
	 * The commissioner hands over a Thread operational dataset -- network
	 * key, channel, PAN ids, the lot -- and expects the node to go and join
	 * with it. This node stores it and cannot yet join, so ConnectNetwork
	 * is answered with a real failure rather than a success it would not
	 * back up. Storing it is still the point: the dataset is the thing
	 * OpenThread needs, and there is no other way to obtain it.
	 */
	uint8_t thread_dataset[MATTER_THREAD_DATASET_MAX];
	size_t thread_dataset_len;
	/** Extended PAN ID, parsed out of the dataset; the network's identity. */
	uint8_t thread_xpanid[MATTER_THREAD_XPANID_LEN];
	bool have_thread_xpanid;
	/** True once the stack accepted the dataset and began attaching. */
	bool thread_started;
	/** NetworkCommissioningStatusEnum from the last network command. */
	uint8_t last_network_status;
	/**
	 * True once a commissioner has finished, which is what makes everything
	 * above permanent. Until then the fail-safe owns it all.
	 */
	bool commissioning_complete;
	/**
	 * True once a CASE session is running, which is the one precondition
	 * CommissioningComplete has.
	 *
	 * The spec will not accept that command over anything else
	 * (general-commissioning-cluster.cpp:548-556 answers
	 * kInvalidAuthentication), and the reason is not ceremony: the command
	 * asserts the commissioner can reach this node operationally, and only
	 * a CASE session is evidence of that. The port sets it; this module has
	 * no way to see a session.
	 */
	bool case_established;
	/**
	 * The NodeOperationalCertStatusEnum the last AddNOC produced, held for
	 * the same reason as last_commissioning_error: the reply is serialised
	 * after the command has run and cannot recompute the verdict.
	 */
	uint8_t last_noc_status;
};

/**
 * Point @p srv at this data model.
 *
 * @param info borrowed, not copied, and must outlive @p srv. Breadcrumb is
 *        written through it.
 */
void matter_clusters_init(struct matter_im_server *srv, struct matter_device_info *info);

/**
 * Undo a commissioning that never finished.
 *
 * This is what the fail-safe MEANS, and until AddNOC there was nothing for it
 * to undo -- so the flag recorded the state without enforcing it. Now a
 * commissioner that gives up half way leaves a fabric behind, and the next
 * attempt is answered TableFull for a reason that has nothing to do with what
 * went wrong. The caller decides when: on this port, when the commissioning
 * link drops.
 *
 * Does nothing once commissioning_complete is set -- a finished fabric is not
 * the fail-safe's to remove.
 *
 * The THREAD attachment is deliberately kept. Strictly the fail-safe owns the
 * network config too, but the commissioner re-sends the identical dataset on
 * the next attempt, and staying attached turns the following attach from two
 * seconds into none. Nothing secret is retained that the next commissioner
 * would not immediately re-supply.
 */
void matter_clusters_failsafe_expire(struct matter_device_info *info);

#ifdef __cplusplus
}
#endif
