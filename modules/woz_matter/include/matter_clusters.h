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
#define MATTER_CLUSTER_DESCRIPTOR              0x001Du
#define MATTER_CLUSTER_ACCESS_CONTROL          0x001Fu
#define MATTER_CLUSTER_OPERATIONAL_CREDENTIALS 0x003Eu
#define MATTER_CLUSTER_DOOR_LOCK               0x0101u

/* Descriptor attributes (Descriptor/AttributeIds.h:19-33). */
#define MATTER_ATTR_DESC_DEVICE_TYPE_LIST 0x0000u
#define MATTER_ATTR_DESC_SERVER_LIST      0x0001u
#define MATTER_ATTR_DESC_CLIENT_LIST      0x0002u
#define MATTER_ATTR_DESC_PARTS_LIST       0x0003u

/*
 * Root Node, revision 3 (matter-devices.xml:31-38, MA-rootdevice).
 *
 * A controller reads this to learn what it has just adopted. Endpoint 0 is
 * always the Root Node; the Door Lock will be a second endpoint with its own
 * device type, listed in this endpoint's PartsList.
 */
#define MATTER_DEVICE_TYPE_ROOT_NODE 0x0016u
#define MATTER_DEVICE_TYPE_ROOT_REV  3u

/*
 * Door Lock, on endpoint 1 (matter-devices.xml, MA-doorlock).
 *
 * This is what makes the accessory a LOCK rather than a bare node: endpoint 0
 * is a Root Node, which carries no functionality, so a controller that adopts
 * this node and finds nothing in PartsList correctly shows an empty tile.
 */
#define MATTER_DEVICE_TYPE_DOOR_LOCK 0x000Au
#define MATTER_DEVICE_TYPE_LOCK_REV  3u

/* Door Lock attributes (DoorLock/AttributeIds.h:24-147). */
#define MATTER_ATTR_DL_LOCK_STATE               0x0000u
#define MATTER_ATTR_DL_LOCK_TYPE                0x0001u
#define MATTER_ATTR_DL_ACTUATOR_ENABLED         0x0002u
#define MATTER_ATTR_DL_OPERATING_MODE           0x0025u
#define MATTER_ATTR_DL_SUPPORTED_OPERATING_MODES 0x0026u

/*
 * The Aliro reader attributes (DoorLock/AttributeIds.h:204-247).
 *
 * These are not decoration. A controller reads them to decide whether this lock
 * can be an Aliro reader at all, and only then does it send
 * SetAliroReaderConfig with the reader private key -- which is the entire
 * reason this node exists.
 */
#define MATTER_ATTR_DL_ALIRO_VERIFICATION_KEY   0x0080u
#define MATTER_ATTR_DL_ALIRO_GROUP_ID           0x0081u
#define MATTER_ATTR_DL_ALIRO_GROUP_SUB_ID       0x0082u
#define MATTER_ATTR_DL_ALIRO_EXPEDITED_VERSIONS 0x0083u
#define MATTER_ATTR_DL_ALIRO_GROUP_RESOLVING_KEY 0x0084u
#define MATTER_ATTR_DL_ALIRO_BLE_UWB_VERSIONS   0x0085u
#define MATTER_ATTR_DL_ALIRO_BLE_ADV_VERSION    0x0086u
#define MATTER_ATTR_DL_ALIRO_ISSUER_KEYS_MAX    0x0087u
#define MATTER_ATTR_DL_ALIRO_ENDPOINT_KEYS_MAX  0x0088u

/*
 * FeatureMap bits this lock claims (DoorLock/Enums.h:510-511).
 *
 * Only the two Aliro bits. Claiming PIN, RFID, schedules or users would commit
 * this node to the whole credential and schedule surface, none of which it has.
 */
#define MATTER_DL_FEATURE_ALIRO_PROVISIONING 0x2000u
#define MATTER_DL_FEATURE_ALIRO_BLE_UWB      0x4000u
/*
 * User (Enums.h:505). Not claimed for its own sake: a real controller invokes
 * GetUser on this endpoint during commissioning and abandons the pairing when
 * it is refused, and Aliro credential keys arrive on the user/credential path
 * rather than through the reader-config commands
 * (ports/esp32/.../aliro_reader_delegate.h:36-43).
 */
#define MATTER_DL_FEATURE_USER 0x0100u

/* User-feature attributes and commands. */
#define MATTER_ATTR_DL_USERS_MAX             0x0011u
#define MATTER_ATTR_DL_CREDS_PER_USER_MAX    0x001Cu
#define MATTER_CMD_DL_SET_USER               0x001Au
#define MATTER_CMD_DL_GET_USER               0x001Bu
#define MATTER_CMD_DL_GET_USER_RESPONSE      0x001Cu

/**
 * SetAliroReaderConfig (DoorLock/CommandIds.h:122-125).
 *
 * The command this whole Matter node exists to receive: it is how Apple Home
 * hands over the reader identity, and it is what makes this device
 * provisionable by its owner rather than only by whoever built the firmware.
 * Always preceded by a TimedRequest, because it must not be replayed.
 */
#define MATTER_CMD_DL_SET_ALIRO_READER_CONFIG 0x0028u

/*
 * LockDoor and UnlockDoor (CommandIds.h:97-98), which are what the tile in a
 * controller's UI actually sends. Answering them is what turns "Matter
 * Accessory / No Response" into a lock a user can press.
 *
 * Both carry an OPTIONAL PINCode field this node ignores: it advertises no
 * PIN_CREDENTIAL feature, so a controller has nothing to send and a PIN that
 * arrived anyway would be a credential class this reader does not implement.
 * Both are Timed Invoke commands, which this node already answers.
 */
#define MATTER_CMD_DL_LOCK_DOOR   0x0000u
#define MATTER_CMD_DL_UNLOCK_DOOR 0x0001u

/*
 * GetCredentialStatus (CommandIds.h:107-109) and its response.
 *
 * Asked immediately after the reader identity lands: the controller is
 * checking whether the credential it is about to install is already there.
 * Refusing it ends the pairing just as surely as refusing GetUser did.
 */
#define MATTER_CMD_DL_GET_CREDENTIAL_STATUS          0x0024u
#define MATTER_CMD_DL_GET_CREDENTIAL_STATUS_RESPONSE 0x0025u

/* GetCredentialStatusResponse fields (Commands.h, its Fields enum). */
#define TAG_CREDSTATUS_EXISTS          0u
#define TAG_CREDSTATUS_USER_INDEX      1u
#define TAG_CREDSTATUS_CREATOR_FABRIC  2u
#define TAG_CREDSTATUS_MODIFIER_FABRIC 3u
#define TAG_CREDSTATUS_NEXT_INDEX      4u
#define TAG_CREDSTATUS_DATA            5u

/* SetAliroReaderConfig fields (DoorLock/Commands.h, its Fields enum). */
#define TAG_ALIRO_CFG_SIGNING_KEY        0u
#define TAG_ALIRO_CFG_VERIFICATION_KEY   1u
#define TAG_ALIRO_CFG_GROUP_ID           2u
#define TAG_ALIRO_CFG_GROUP_RESOLVING_KEY 3u

#define MATTER_ALIRO_SIGNING_KEY_LEN      32u
#define MATTER_ALIRO_VERIFICATION_KEY_LEN 65u

/*
 * SetCredential (0x0022) and its response (0x0023).
 *
 * The Aliro credential itself: CredentialData is an uncompressed P-256 public
 * key, and installing it is what turns a reader with an identity into one that
 * will actually open for a phone.
 */
#define MATTER_CMD_DL_SET_CREDENTIAL          0x0022u
#define MATTER_CMD_DL_SET_CREDENTIAL_RESPONSE 0x0023u

#define TAG_SETCRED_OPERATION 0u
#define TAG_SETCRED_CREDENTIAL 1u
#define TAG_SETCRED_DATA       2u
#define TAG_SETCRED_USER_INDEX 3u

/* CredentialStruct (DoorLock/Structs.h:48-49), nested inside field 1. */
#define TAG_CREDSTRUCT_TYPE  0u
#define TAG_CREDSTRUCT_INDEX 1u

/* SetCredentialResponse fields. */
#define TAG_SETCREDRESP_STATUS     0u
#define TAG_SETCREDRESP_USER_INDEX 1u
#define TAG_SETCREDRESP_NEXT_INDEX 2u

/*
 * Aliro credential types (DoorLock/Enums.h, CredentialTypeEnum). Only these
 * three carry a reader trust anchor; PIN, RFID, fingerprint and face are the
 * surfaces this node does not claim.
 */
#define MATTER_DL_CRED_ALIRO_ISSUER_KEY         6u
#define MATTER_DL_CRED_ALIRO_EVICTABLE_ENDPOINT 7u
#define MATTER_DL_CRED_ALIRO_ENDPOINT_KEY       8u

/**
 * Install an Aliro credential public key, set by the port.
 *
 * Returns 0 when added, 1 when already present, negative on a bad point or a
 * full/failed store -- the contract aliro_reader_provision_add_trust() already
 * has, passed through unchanged so the module invents no policy of its own.
 */
/* SetUser fields (DoorLock/Commands.h, SetUser::Fields). Numbered from
 * kOperationType = 0, so every later field sits one above its GetUserResponse
 * twin -- reading one enum for the other silently shifts every value. */
#define TAG_SETUSER_OPERATION       0u
#define TAG_SETUSER_INDEX           1u
#define TAG_SETUSER_NAME            2u
#define TAG_SETUSER_UNIQUE_ID       3u
#define TAG_SETUSER_STATUS          4u
#define TAG_SETUSER_TYPE            5u
#define TAG_SETUSER_CREDENTIAL_RULE 6u

/** One user slot. Reported by GetUser, filled by SetUser. */
struct matter_user {
	uint32_t unique_id;
	uint8_t status;
	uint8_t type;
	uint8_t credential_rule;
	uint8_t creator_fabric;
	uint8_t modifier_fabric;
	bool in_use;
};

/* GetUserResponse fields (DoorLock/Commands.h, GetUserResponse::Fields). */
#define TAG_GETUSER_INDEX          0u
#define TAG_GETUSER_NAME           1u
#define TAG_GETUSER_UNIQUE_ID      2u
#define TAG_GETUSER_STATUS         3u
#define TAG_GETUSER_TYPE           4u
#define TAG_GETUSER_CREDENTIAL_RULE 5u
#define TAG_GETUSER_CREDENTIALS    6u
#define TAG_GETUSER_CREATOR_FABRIC 7u
#define TAG_GETUSER_MODIFIER_FABRIC 8u
#define TAG_GETUSER_NEXT_INDEX     9u

/**
 * How many user slots this lock reports.
 *
 * Reported, not stored: this node holds no user database yet. The count has to
 * be non-zero because a lock claiming the User feature with room for nobody is
 * not a coherent answer.
 */
#define MATTER_DL_USERS_MAX          10u
#define MATTER_DL_CREDS_PER_USER_MAX 5u

/* LockState (DoorLock/Enums.h:95-99) and OperatingMode (Enums.h:278-284). */
#define MATTER_DL_LOCK_STATE_LOCKED     1u
#define MATTER_DL_LOCK_STATE_UNLOCKED   2u
#define MATTER_DL_OPERATING_MODE_NORMAL 0u
/** SupportedOperatingModes is a bitmap; bit 0 is Normal and it is the only one. */
#define MATTER_DL_SUPPORTED_OPERATING_MODES 0x0001u

/*
 * The one Aliro protocol version this reader speaks, big-endian, reported for
 * both the expedited and BLE-UWB lists. Same value the ESP32 lock advertises
 * (ports/esp32/.../aliro_reader_delegate.cpp:47), which is the port that has
 * actually been provisioned by Apple Home.
 */
#define MATTER_ALIRO_PROTOCOL_VERSION 0x0100u
/** 0 is the only defined Aliro BLE advertising version. */
#define MATTER_ALIRO_BLE_ADV_VERSION 0u
/** Matches the ESP32 lock's kAliroKeysSupported (aliro_reader_delegate.h:94). */
#define MATTER_ALIRO_KEYS_SUPPORTED 10u
/** Aliro group identifier, sub-identifier and resolving key are all 16 bytes. */
#define MATTER_ALIRO_GROUP_ID_LEN 16u

/** Access Control attributes (access-control-cluster.cpp, AclAttribute). */
#define MATTER_ATTR_AC_ACL                 0x0000u
#define MATTER_ATTR_AC_EXTENSION           0x0001u
#define MATTER_ATTR_AC_SUBJECTS_PER_ENTRY  0x0002u
#define MATTER_ATTR_AC_TARGETS_PER_ENTRY   0x0003u
#define MATTER_ATTR_AC_ENTRIES_PER_FABRIC  0x0004u

/**
 * How much of an ACL this node will hold.
 *
 * One fabric, and the spec's floor is four entries per fabric; a commissioner
 * writes the whole list at once, so this bounds the encoded list rather than
 * any single entry.
 */
#define MATTER_ACL_MAX 256u

/** FeatureMap, on every cluster (GlobalAttributeIds.h). */
#define MATTER_ATTR_FEATURE_MAP 0xFFFCu

/* BasicInformation attributes (BasicInformation/AttributeIds.h:19-77). */
#define MATTER_ATTR_BASIC_DATA_MODEL_REVISION    0x0000u
#define MATTER_ATTR_BASIC_VENDOR_NAME            0x0001u
#define MATTER_ATTR_BASIC_VENDOR_ID              0x0002u
#define MATTER_ATTR_BASIC_PRODUCT_NAME           0x0003u
#define MATTER_ATTR_BASIC_PRODUCT_ID             0x0004u
#define MATTER_ATTR_BASIC_NODE_LABEL             0x0005u
#define MATTER_ATTR_BASIC_LOCATION               0x0006u
#define MATTER_ATTR_BASIC_HARDWARE_VERSION       0x0007u
#define MATTER_ATTR_BASIC_HARDWARE_VERSION_STR   0x0008u
#define MATTER_ATTR_BASIC_SOFTWARE_VERSION       0x0009u
#define MATTER_ATTR_BASIC_SOFTWARE_VERSION_STR   0x000Au
#define MATTER_ATTR_BASIC_SERIAL_NUMBER          0x000Fu
#define MATTER_ATTR_BASIC_UNIQUE_ID              0x0012u
#define MATTER_ATTR_BASIC_CAPABILITY_MINIMA      0x0013u
#define MATTER_ATTR_BASIC_SPECIFICATION_VERSION  0x0015u
#define MATTER_ATTR_BASIC_MAX_PATHS_PER_INVOKE   0x0016u

/*
 * What this node reports about itself.
 *
 * DataModelRevision and SpecificationVersion are the 1.2 values, not the SDK's
 * current 19 / 0x01050000 (SpecificationDefinedRevisions.h:44,53). Claiming 1.5
 * would claim features this node does not have; 1.2 is what it was written
 * against and what SPECIFICATION_VERSION in the vendored SDK says.
 */
#define MATTER_DATA_MODEL_REVISION   17u
#define MATTER_SPECIFICATION_VERSION 0x01020000u

/*
 * CapabilityMinima: the spec's floors, which are also what this node can do
 * (InteractionModelEngine.h:110-111). One CASE session and one subscription are
 * what it actually holds -- but the floor is 3 and reporting less than the floor
 * is not a legal answer, so these are the honest minimum it may claim.
 */
#define MATTER_CASE_SESSIONS_PER_FABRIC  3u
#define MATTER_SUBSCRIPTIONS_PER_FABRIC  3u

/** CHIP_CONFIG_MAX_PATHS_PER_INVOKE (CHIPConfig.h:1877), and what this node parses. */
#define MATTER_MAX_PATHS_PER_INVOKE 1u

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
#define MATTER_ATTR_OC_NOCS                 0x0000u
#define MATTER_ATTR_OC_FABRICS              0x0001u
#define MATTER_ATTR_OC_SUPPORTED_FABRICS    0x0002u
#define MATTER_ATTR_OC_COMMISSIONED_FABRICS 0x0003u
#define MATTER_ATTR_OC_TRUSTED_ROOTS        0x0004u
#define MATTER_ATTR_OC_CURRENT_FABRIC_INDEX 0x0005u

/**
 * How many fabrics this node can hold at once.
 *
 * Two, which is not a round number -- it is what a single Apple Home needs.
 * The phone and the home hub commission the device onto SEPARATE fabrics, each
 * with its own trusted root and its own operational key, and a node that
 * advertises one answers the second AddNOC with TABLE_FULL and is never
 * adopted. The spec's floor is five; there is a struct matter_fabric of RAM
 * behind every entry and this part has 128 KB in total.
 */
#define MATTER_SUPPORTED_FABRICS 2u

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

/** Every Matter node's root endpoint is 0. */
#define MATTER_ENDPOINT_ROOT 0u
/** The Door Lock. Listed in the root endpoint's PartsList. */
#define MATTER_ENDPOINT_LOCK 1u

struct matter_device_info {
	uint16_t vendor_id;
	uint16_t product_id;
	/**
	 * AliroReaderGroupSubIdentifier, filled by the port.
	 *
	 * Here rather than generated in this module because woz_matter carries
	 * no RNG and no storage seam, and this value has to be stable for the
	 * life of the reader group -- a controller that reads a different
	 * sub-identifier after a reboot is entitled to treat this as a
	 * different reader. The port owns both the entropy and the store.
	 */
	uint8_t aliro_group_sub_id[MATTER_ALIRO_GROUP_ID_LEN];
	/**
	 * The UserIndex the last GetUser asked for, held between running the
	 * command and serialising its response -- matter_im_command_fields_fn
	 * is pure and cannot recompute it. Same reason as
	 * @ref last_commissioning_error below.
	 */
	uint16_t last_user_index;
	/** The status SetCredential decided, held for its response encoder. */
	uint8_t last_credential_status;
	/**
	 * What LockState reports, and what LockDoor/UnlockDoor change.
	 *
	 * Zero is not a legal LockState, so it means "never set" and is reported
	 * as Locked -- a reader that has been asked nothing is not unlocked.
	 * There is still no actuator behind this: it moves no bolt, and saying
	 * Unlocked is a claim about this node's state rather than about a door.
	 * It matters anyway, because a controller draws the tile from it and
	 * refuses to send UnlockDoor to something already reporting Unlocked.
	 */
	uint8_t lock_state;
	/**
	 * The user table, indexed from 0 for slot 1.
	 *
	 * Small and real rather than stubbed: Apple writes a user with SetUser
	 * and immediately reads it back with GetUser, so a node that accepts
	 * the write and then reports an empty slot has told the controller two
	 * different things. The user NAME is deliberately not stored -- it is
	 * nullable, nothing here displays it, and it is the only field that
	 * would cost real RAM.
	 */
	struct matter_user users[MATTER_DL_USERS_MAX];
	/**
	 * The fabric index of the session being served right now, set by the
	 * port before it dispatches each secure message.
	 *
	 * CurrentFabricIndex is FABRIC-SCOPED: it must answer "which fabric are
	 * you, to me", and the answer depends entirely on who is asking. While
	 * this was hardcoded to the first live fabric, the home hub read it over
	 * its own session, was told it was fabric 1, concluded it was not on the
	 * fabric it had just joined, and sent RemoveFabric. Zero means no secure
	 * session, which is not a legal answer to anyone and is why nothing may
	 * read it outside one.
	 */
	uint8_t accessing_fabric_index;

	/*
	 * ---- the Aliro reader identity, once Apple has delivered it --------
	 *
	 * The SIGNING KEY is deliberately absent. It is the reader's private
	 * key: it goes straight to @ref aliro_reader_config_cb, which persists
	 * it, and is never held here, never read back as an attribute, and
	 * never logged. What stays is only what a controller may read back.
	 */
	uint8_t aliro_verification_key[MATTER_ALIRO_VERIFICATION_KEY_LEN];
	uint8_t aliro_group_id[MATTER_ALIRO_GROUP_ID_LEN];
	uint8_t aliro_group_resolving_key[MATTER_ALIRO_GROUP_ID_LEN];
	bool have_aliro_group_resolving_key;
	bool have_aliro_reader_config;
	/**
	 * Where the identity actually lands, set by the port.
	 *
	 * A function pointer rather than a link seam so the module keeps
	 * building for host tests, where there is no store to write to and
	 * NULL is the honest answer. Returns 0 on success; anything else makes
	 * the command report FAILURE rather than claiming an identity was kept.
	 */
	int (*aliro_credential_cb)(uint8_t credential_type,
				   const uint8_t public_key[MATTER_ALIRO_VERIFICATION_KEY_LEN]);
	int (*aliro_reader_config_cb)(const uint8_t signing_key[MATTER_ALIRO_SIGNING_KEY_LEN],
				      const uint8_t verification_key[MATTER_ALIRO_VERIFICATION_KEY_LEN],
				      const uint8_t group_id[MATTER_ALIRO_GROUP_ID_LEN],
				      const uint8_t *group_resolving_key);
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
	struct matter_fabric fabrics[MATTER_SUPPORTED_FABRICS];
	/**
	 * The node's single intermediate-certificate slot, shared by every
	 * fabric because at most one has ever needed it. See the note on
	 * matter_fabric.icac_len for what that costs.
	 */
	struct matter_icac_slot icac;

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

	/*
	 * ---- access control -----------------------------------------------
	 *
	 * The commissioner's last act is writing itself an ACL entry granting
	 * Administer over CASE. Stored as the TLV that arrived, because the
	 * only thing this node does with it is hand it back on a read.
	 *
	 * RECORDED, NOT ENFORCED. Nothing consults this before serving a
	 * request; every peer that completes CASE is already trusted with
	 * everything. Storing it is what makes the commissioner's write
	 * truthful, and enforcing it is a separate piece of work that this
	 * comment exists so nobody assumes is done.
	 */
	uint8_t acl[MATTER_ACL_MAX];
	size_t acl_len;
	/**
	 * The NodeOperationalCertStatusEnum the last AddNOC produced, held for
	 * the same reason as last_commissioning_error: the reply is serialised
	 * after the command has run and cannot recompute the verdict.
	 */
	uint8_t last_noc_status;
	/**
	 * The index AddNOC assigned, held for the NOCResponse.
	 *
	 * The reply is serialised after the command has run and cannot recompute
	 * it -- and with more than one slot, "the fabric that was just created"
	 * is no longer the same thing as "the only one".
	 */
	uint8_t last_noc_index;
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
