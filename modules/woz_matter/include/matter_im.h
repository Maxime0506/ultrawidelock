/**
 * @file matter_im.h — the Interaction Model, as far as a commissioner needs it.
 *
 * Once PASE is done the commissioner stops speaking Secure Channel and starts
 * asking questions. The first one is a ReadRequest, and until something answers
 * it the phone waits, gives up, and shows "pairing failed" with no further clue.
 *
 *   in    ReadRequest   (protocol 0x0001, opcode 0x02)
 *   out   ReportData    (protocol 0x0001, opcode 0x05)
 *
 * This is the read half only. Write, Invoke and Subscribe are separate messages
 * and land when something needs them; commissioning cannot start without Read.
 *
 * It holds no device data. Which endpoints exist and what their attributes say
 * is matter_clusters.h's, reached through @ref matter_im_server, so the wire
 * format can be tested without a device and the device without a wire.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 4 of internal/cdk-matter-plan.md.
 *
 * Every constant here is transcribed from the SDK in
 * workspace/modules/lib/matter/src, cited at its use. The message shapes come
 * from src/app/MessageDef, whose builders name the container type for each
 * information block -- AttributePathIB is a LIST and everything else here is a
 * STRUCTURE, which is not guessable and not symmetric.
 *
 * Independently confirmed on the wire: a real iPhone's first post-PASE message
 * decoded against these tags exactly, AttributePathIB arriving as 0x17 (list).
 * tests/host/test_matter_im.c replays those 106 bytes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_status.h"
#include "matter_tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol 0x0001 message types (protocols/interaction_model/Constants.h:59-70). */
#define MATTER_IM_OP_STATUS_RESPONSE         0x01u
#define MATTER_IM_OP_READ_REQUEST            0x02u
#define MATTER_IM_OP_REPORT_DATA             0x05u
#define MATTER_IM_OP_INVOKE_COMMAND_REQUEST  0x08u
#define MATTER_IM_OP_INVOKE_COMMAND_RESPONSE 0x09u

/**
 * Interaction Model revision this node claims
 * (app/SpecificationDefinedRevisions.h:34). Sent under tag 0xFF (line 35),
 * which is why it is a define rather than an argument: a peer that disagrees
 * about the revision disagrees about the whole message.
 */
#define MATTER_IM_REVISION 12u

/*
 * Status codes, from protocols/interaction_model/StatusCodeList.h. Only the
 * ones a read can produce; the list is long and unused entries are noise.
 */
#define MATTER_IM_STATUS_SUCCESS               0x00u /* line 31 */
#define MATTER_IM_STATUS_FAILURE               0x01u /* line 32 */
#define MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT  0x7Fu /* line 35 */
#define MATTER_IM_STATUS_UNSUPPORTED_COMMAND   0x81u /* line 37 */
#define MATTER_IM_STATUS_INVALID_COMMAND       0x85u /* line 41 */
#define MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE 0x86u /* line 42 */
#define MATTER_IM_STATUS_UNSUPPORTED_CLUSTER   0xC3u /* line 68 */

/**
 * How many attribute paths one ReadRequest may carry before this refuses it.
 *
 * A real iPhone's commissioning read asked for NINE. The bound exists so a peer
 * cannot choose how much stack this uses, and it is a refusal rather than a
 * truncation: answering some of what was asked, silently, would look to the
 * commissioner like the device genuinely has nothing more.
 */
#define MATTER_IM_MAX_PATHS 16

/**
 * One requested path. An absent field is a WILDCARD, and that distinction
 * decides how a path that matches nothing is answered -- see
 * matter_im_report_data_encode().
 */
struct matter_im_path {
	uint16_t endpoint;
	uint32_t cluster;
	uint32_t attribute;
	bool have_endpoint;
	bool have_cluster;
	bool have_attribute;
};

/** True when any component is wildcarded (app/AttributePathParams.h:57-59). */
static inline bool matter_im_path_is_wildcard(const struct matter_im_path *p)
{
	return !p->have_endpoint || !p->have_cluster || !p->have_attribute;
}

/** A decoded ReadRequest. */
struct matter_im_read {
	struct matter_im_path paths[MATTER_IM_MAX_PATHS];
	uint8_t n_paths;
	/** As sent; recorded rather than acted on, since nothing here is fabric-scoped yet. */
	bool fabric_filtered;
};

/**
 * Decode a ReadRequestMessage (app/MessageDef/ReadRequestMessage.h).
 *
 * @return MATTER_OK; MATTER_E_NOSPACE when the peer asked for more than
 *         MATTER_IM_MAX_PATHS paths; MATTER_E_INVAL for a malformed message; or
 *         whatever the TLV decoder returned.
 */
int matter_im_read_request_decode(const uint8_t *tlv, size_t len, struct matter_im_read *out);

/**
 * What answering this path will produce, WITHOUT writing anything.
 *
 * Split from writing the value on purpose. An information block is built
 * outside-in -- container, data version, path, then the value -- so by the time
 * a writing callback could report a failure, three fields are already committed
 * and TLV offers no way to take them back. Asking first means the encoder never
 * has to unwind, which matters because the writer latches its first error and
 * turns later calls into no-ops: an unwind would have to reason about container
 * depth that was never incremented.
 *
 * @return MATTER_IM_STATUS_SUCCESS, or the status explaining why not --
 *         UNSUPPORTED_ATTRIBUTE when the cluster exists but the attribute does
 *         not, UNSUPPORTED_CLUSTER when the endpoint exists but the cluster
 *         does not, UNSUPPORTED_ENDPOINT when it does not
 *         (app/data-model-provider/MetadataLookup.cpp:68-88 chooses in that
 *         order).
 */
typedef uint8_t (*matter_im_status_fn)(void *ctx, uint16_t endpoint, uint32_t cluster,
				       uint32_t attribute);

/**
 * Write one attribute's value. Called only after @ref matter_im_status_fn
 * returned SUCCESS for the same path, so it has nothing to refuse.
 *
 * @param w write EXACTLY ONE element, tagged @p tag. A structure is one
 *          element; two siblings are not. Errors need no checking -- the writer
 *          latches them and the encoder reports them once at the end.
 */
typedef void (*matter_im_value_fn)(void *ctx, uint16_t endpoint, uint32_t cluster,
				   uint32_t attribute, struct matter_tlv_writer *w,
				   matter_tlv_tag_t tag);

/**
 * Does @p endpoint carry @p cluster?
 *
 * Only consulted for wildcard paths, where a miss must be silent rather than an
 * error, so this decides between answering and saying nothing at all.
 */
typedef bool (*matter_im_has_cluster_fn)(void *ctx, uint16_t endpoint, uint32_t cluster);

/** One command a commissioner asked this node to run. */
struct matter_im_invoke {
	uint16_t endpoint;
	uint32_t cluster;
	uint32_t command;
	/** The command's arguments, as TLV. Points into the caller's buffer. */
	const uint8_t *fields;
	size_t fields_len;
	bool has_fields;
	/**
	 * The commissioner's own handle for this command, echoed in the response
	 * when it sent one. Batched invokes need it to match responses to
	 * requests; a single invoke does not, and Apple's ArmFailSafe omits it.
	 */
	uint64_t command_ref;
	bool has_command_ref;
	bool suppress_response;
	bool timed_request;
};

/**
 * Run one command.
 *
 * Called EXACTLY ONCE per request, which is why it is separate from writing the
 * reply: this is where side effects belong. Anything the response needs must be
 * recorded in the device's own state here, because
 * @ref matter_im_command_fields_fn runs afterwards and must be pure.
 *
 * @param response_command receives the command id to answer with. Ignored
 *        unless SUCCESS is returned.
 * @return MATTER_IM_STATUS_SUCCESS to send a command response, or the status to
 *         send instead -- UNSUPPORTED_COMMAND when the cluster exists but the
 *         command does not, INVALID_COMMAND when its arguments do not parse.
 */
typedef uint8_t (*matter_im_command_fn)(void *ctx, const struct matter_im_invoke *inv,
					uint32_t *response_command);

/**
 * Write the fields of the response @ref matter_im_command_fn chose.
 *
 * Pure: it serialises what the command already computed. Write exactly one
 * element, tagged @p tag, as with @ref matter_im_value_fn.
 */
typedef void (*matter_im_command_fields_fn)(void *ctx, uint16_t endpoint, uint32_t cluster,
					    uint32_t response_command, struct matter_tlv_writer *w,
					    matter_tlv_tag_t tag);

/**
 * List the attributes of one cluster, for expanding a wildcard read.
 *
 * @param out receives a pointer to a static array of attribute ids.
 * @return how many, or 0 when the cluster is not on that endpoint.
 */
typedef size_t (*matter_im_list_attrs_fn)(void *ctx, uint16_t endpoint, uint32_t cluster,
					  const uint32_t **out);

struct matter_im_server {
	matter_im_status_fn status;
	matter_im_value_fn value;
	matter_im_has_cluster_fn has_cluster;
	matter_im_list_attrs_fn list_attrs;
	matter_im_command_fn command;
	matter_im_command_fields_fn command_fields;
	void *ctx;
};

/**
 * Decode an InvokeRequestMessage (app/MessageDef/InvokeRequestMessage.h).
 *
 * Exactly one command per message. Matter allows a batch, and this refuses one
 * with MATTER_E_NOSPACE rather than silently running the first: a commissioner
 * that batched two commands and saw one response would be entitled to assume
 * both ran.
 *
 * @return MATTER_OK, MATTER_E_NOSPACE for a batch, MATTER_E_INVAL for a
 *         malformed message, or whatever the TLV decoder returned.
 */
int matter_im_invoke_request_decode(const uint8_t *tlv, size_t len, struct matter_im_invoke *out);

/**
 * Run @p inv and encode the InvokeResponseMessage answering it.
 *
 * The command runs even when the peer set SuppressResponse -- it asked for the
 * effect, not for silence about it -- but then @p out_len is set to 0 and there
 * is nothing to send.
 *
 * @return MATTER_OK, MATTER_E_NOSPACE, or MATTER_E_INVAL.
 */
int matter_im_invoke_response_encode(const struct matter_im_server *srv,
				     const struct matter_im_invoke *inv, uint8_t *out, size_t cap,
				     size_t *out_len);

/** What encoding a report had to leave out. Worth logging; none of it is fatal. */
struct matter_im_report_stats {
	/** Wildcard paths skipped because the cluster is not on that endpoint. Correct. */
	uint8_t skipped_wildcard;
	/**
	 * Wildcard paths over a cluster this node DOES have.
	 *
	 * Answering one means enumerating a cluster's attributes, which nothing
	 * has asked for yet and which this does not do. The path is skipped and
	 * counted, so an under-reported read shows up in a log line instead of
	 * looking to the commissioner like an empty cluster.
	 */
	uint8_t unexpanded_wildcard;
};

/**
 * Encode the ReportData answering @p req.
 *
 * The rule that is easy to get backwards, and is not symmetric:
 *
 *   - A CONCRETE path naming something unsupported gets an AttributeStatusIB.
 *     Reporting nothing would tell the commissioner the attribute is absent
 *     from a device that never said so (reporting/Engine.cpp:92-106, 495).
 *   - A WILDCARD path matching nothing gets SILENCE. An error would claim the
 *     whole expansion failed (AttributePathExpandIterator.cpp:239-255).
 *
 * SuppressResponse is set, because this answers a Read rather than a Subscribe
 * and CHIP sets it for exactly that case (reporting/Engine.cpp:834-836). The
 * commissioner therefore sends no StatusResponse and the exchange simply ends.
 *
 * Chunking is not implemented: an answer too large for @p cap fails rather than
 * setting MoreChunkedMessages. A truncated report is indistinguishable from a
 * complete one to the peer, so it must not be possible to send one by accident.
 *
 * @param stats optional; may be NULL.
 * @return MATTER_OK, MATTER_E_NOSPACE if the report does not fit, or
 *         MATTER_E_INVAL.
 */
int matter_im_report_data_encode(const struct matter_im_server *srv,
				 const struct matter_im_read *req, uint8_t *out, size_t cap,
				 size_t *out_len, struct matter_im_report_stats *stats);

#ifdef __cplusplus
}
#endif
