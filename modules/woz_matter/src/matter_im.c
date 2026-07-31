/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * See matter_im.h. Tag numbers are transcribed from
 * workspace/modules/lib/matter/src/app/MessageDef/, cited at each use, because
 * they are the one thing here that cannot be derived or sanity-checked at
 * runtime: a wrong tag encodes cleanly and is refused by the peer in silence.
 */
#include "matter_im.h"

#include <string.h>

/* ReadRequestMessage.h:41-47 */
#define TAG_READ_ATTRIBUTE_PATHS 0u
#define TAG_READ_FABRIC_FILTERED 3u

/* AttributePathIB.h:40-45. The IB itself is a LIST, not a structure. */
#define TAG_PATH_ENDPOINT  2u
#define TAG_PATH_CLUSTER   3u
#define TAG_PATH_ATTRIBUTE 4u

/* ReportDataMessage.h:43-47 */
#define TAG_REPORT_ATTRIBUTE_REPORTS 1u
#define TAG_REPORT_SUPPRESS_RESPONSE 4u

/* AttributeReportIB.h:37-38 */
#define TAG_AREPORT_STATUS 0u
#define TAG_AREPORT_DATA   1u

/* AttributeDataIB.h:37-39 */
#define TAG_ADATA_VERSION 0u
#define TAG_ADATA_PATH    1u
#define TAG_ADATA_DATA    2u

/* AttributeStatusIB.h:37-38 */
#define TAG_ASTATUS_PATH   0u
#define TAG_ASTATUS_STATUS 1u

/* StatusIB.h:67 */
#define TAG_STATUS_STATUS 0u

/* SpecificationDefinedRevisions.h:35 */
#define TAG_IM_REVISION 0xFFu

/* InvokeRequestMessage.h:41-43 */
#define TAG_INVOKE_SUPPRESS_RESPONSE 0u
#define TAG_INVOKE_TIMED_REQUEST     1u
#define TAG_INVOKE_REQUESTS          2u

/* InvokeResponseMessage.h:41-43 */
#define TAG_IRESP_SUPPRESS_RESPONSE 0u
#define TAG_IRESP_RESPONSES         1u
#define TAG_IRESP_MORE_CHUNKS       2u

/* CommandDataIB.h:37-39 */
#define TAG_CMDDATA_PATH   0u
#define TAG_CMDDATA_FIELDS 1u
#define TAG_CMDDATA_REF    2u

/* CommandPathIB.h:40-42. A LIST, like AttributePathIB. */
#define TAG_CMDPATH_ENDPOINT 0u
#define TAG_CMDPATH_CLUSTER  1u
#define TAG_CMDPATH_COMMAND  2u

/* InvokeResponseIB.h:37-38 */
#define TAG_IRESPIB_COMMAND 0u
#define TAG_IRESPIB_STATUS  1u

/* CommandStatusIB.h:37-39 */
#define TAG_CMDSTATUS_PATH   0u
#define TAG_CMDSTATUS_STATUS 1u
#define TAG_CMDSTATUS_REF    2u

/**
 * Data version reported for every attribute.
 *
 * Real devices bump this when a cluster's data changes so a commissioner can
 * skip re-reading it. Nothing on this node changes yet, so a constant is honest
 * rather than lazy; it becomes a per-cluster counter when something can write.
 */
#define DATA_VERSION 1u

/** Decode one AttributePathIB. The reader is positioned ON the list element. */
static int decode_path(struct matter_tlv_reader *r, struct matter_im_path *p)
{
	int rc;

	memset(p, 0, sizeof(*p));

	rc = matter_tlv_enter(r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		uint64_t v;

		rc = matter_tlv_next(r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		/*
		 * Unknown tags are SKIPPED, not refused. A newer commissioner may
		 * send fields this node has never heard of -- Node and
		 * EnableTagCompression already exist -- and refusing the whole
		 * read over one would break against every future revision.
		 */
		if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_PATH_ENDPOINT)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT16_MAX) {
				return MATTER_E_INVAL;
			}
			p->endpoint = (uint16_t)v;
			p->have_endpoint = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_PATH_CLUSTER)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT32_MAX) {
				return MATTER_E_INVAL;
			}
			p->cluster = (uint32_t)v;
			p->have_cluster = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_PATH_ATTRIBUTE)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT32_MAX) {
				return MATTER_E_INVAL;
			}
			p->attribute = (uint32_t)v;
			p->have_attribute = true;
		}
	}

	return matter_tlv_exit(r);
}

int matter_im_read_request_decode(const uint8_t *tlv, size_t len, struct matter_im_read *out)
{
	struct matter_tlv_reader r;
	int rc;

	if (tlv == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}

	memset(out, 0, sizeof(*out));
	matter_tlv_reader_init(&r, tlv, len);

	/* The message is one anonymous structure. */
	rc = matter_tlv_next(&r);
	if (rc != MATTER_OK) {
		return (rc == MATTER_END) ? MATTER_E_INVAL : rc;
	}
	if (matter_tlv_element_type(&r) != MATTER_TLV_STRUCTURE) {
		return MATTER_E_TYPE;
	}
	rc = matter_tlv_enter(&r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		rc = matter_tlv_next(&r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_READ_ATTRIBUTE_PATHS)) {
			if (matter_tlv_element_type(&r) != MATTER_TLV_ARRAY) {
				return MATTER_E_TYPE;
			}
			rc = matter_tlv_enter(&r);
			if (rc != MATTER_OK) {
				return rc;
			}
			for (;;) {
				rc = matter_tlv_next(&r);
				if (rc == MATTER_END) {
					break;
				}
				if (rc != MATTER_OK) {
					return rc;
				}
				if (matter_tlv_element_type(&r) != MATTER_TLV_LIST) {
					return MATTER_E_TYPE;
				}
				if (out->n_paths >= MATTER_IM_MAX_PATHS) {
					return MATTER_E_NOSPACE;
				}
				rc = decode_path(&r, &out->paths[out->n_paths]);
				if (rc != MATTER_OK) {
					return rc;
				}
				out->n_paths++;
			}
			rc = matter_tlv_exit(&r);
			if (rc != MATTER_OK) {
				return rc;
			}
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_READ_FABRIC_FILTERED)) {
			rc = matter_tlv_get_bool(&r, &out->fabric_filtered);
			if (rc != MATTER_OK) {
				return rc;
			}
		}
		/* Event paths, filters and the revision are read past deliberately:
		 * this node has no events, and the revision is the peer's claim
		 * about itself, not a demand on the answer. */
	}

	return matter_tlv_exit(&r);
}

/** Write one AttributePathIB. Concrete by construction: only reported paths. */
static void put_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag,
		     const struct matter_im_path *p)
{
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_LIST);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_PATH_ENDPOINT), p->endpoint);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_PATH_CLUSTER), p->cluster);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_PATH_ATTRIBUTE), p->attribute);
	(void)matter_tlv_end_container(w);
}

/**
 * Append one AttributeReportIB carrying a status rather than a value.
 *
 * Errors are not checked here because the writer latches the first one and
 * makes every later call a no-op; matter_tlv_writer_finish() reports it once.
 */
static void put_status_report(struct matter_tlv_writer *w, const struct matter_im_path *p,
			      uint8_t status)
{
	(void)matter_tlv_start_container(w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_start_container(w, MATTER_TLV_CTX(TAG_AREPORT_STATUS),
					 MATTER_TLV_STRUCTURE);
	put_path(w, MATTER_TLV_CTX(TAG_ASTATUS_PATH), p);
	(void)matter_tlv_start_container(w, MATTER_TLV_CTX(TAG_ASTATUS_STATUS),
					 MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_STATUS_STATUS), status);
	(void)matter_tlv_end_container(w);
	(void)matter_tlv_end_container(w);
	(void)matter_tlv_end_container(w);
}

/**
 * Append one AttributeReportIB for a CONCRETE path: the value, or the status
 * saying why not.
 *
 * The status is settled before a byte is committed. Deciding afterwards would
 * mean unwinding a half-written information block, and the writer latches its
 * first error and turns later calls into no-ops -- so an unwind would have to
 * reason about container depth that was never incremented.
 */
static void put_report(struct matter_tlv_writer *w, const struct matter_im_server *srv,
		       const struct matter_im_path *p)
{
	uint8_t status = srv->status(srv->ctx, p->endpoint, p->cluster, p->attribute);

	if (status != MATTER_IM_STATUS_SUCCESS) {
		put_status_report(w, p, status);
		return;
	}

	(void)matter_tlv_start_container(w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_start_container(w, MATTER_TLV_CTX(TAG_AREPORT_DATA), MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_ADATA_VERSION), DATA_VERSION);
	put_path(w, MATTER_TLV_CTX(TAG_ADATA_PATH), p);
	srv->value(srv->ctx, p->endpoint, p->cluster, p->attribute, w,
		   MATTER_TLV_CTX(TAG_ADATA_DATA));
	(void)matter_tlv_end_container(w);
	(void)matter_tlv_end_container(w);
}

int matter_im_report_data_encode(const struct matter_im_server *srv,
				 const struct matter_im_read *req, uint8_t *out, size_t cap,
				 size_t *out_len, struct matter_im_report_stats *stats)
{
	struct matter_tlv_writer w;
	uint8_t i;

	if (srv == NULL || srv->status == NULL || srv->value == NULL || srv->has_cluster == NULL ||
	    req == NULL || out == NULL || out_len == NULL) {
		return MATTER_E_INVAL;
	}
	if (stats != NULL) {
		memset(stats, 0, sizeof(*stats));
	}

	matter_tlv_writer_init(&w, out, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_REPORT_ATTRIBUTE_REPORTS),
					 MATTER_TLV_ARRAY);

	for (i = 0; i < req->n_paths; i++) {
		const struct matter_im_path *p = &req->paths[i];

		if (matter_im_path_is_wildcard(p)) {
			const uint32_t *attrs = NULL;
			size_t n_attrs = 0u;
			size_t k;

			/*
			 * An endpoint or cluster wildcard would need this node to
			 * enumerate its own endpoints, which it does not do: there
			 * is one, and inventing a registry for it would be
			 * scaffolding around a constant. Counted, not silent.
			 */
			if (!p->have_endpoint || !p->have_cluster) {
				if (stats != NULL) {
					stats->unexpanded_wildcard++;
				}
				continue;
			}
			/*
			 * Absent cluster: SILENCE, not an error -- the asymmetry
			 * documented in matter_im.h.
			 */
			if (!srv->has_cluster(srv->ctx, p->endpoint, p->cluster)) {
				if (stats != NULL) {
					stats->skipped_wildcard++;
				}
				continue;
			}

			/* Present: report every attribute of it. */
			if (srv->list_attrs != NULL) {
				n_attrs =
					srv->list_attrs(srv->ctx, p->endpoint, p->cluster, &attrs);
			}
			if (n_attrs == 0u || attrs == NULL) {
				if (stats != NULL) {
					stats->unexpanded_wildcard++;
				}
				continue;
			}
			for (k = 0; k < n_attrs; k++) {
				struct matter_im_path one = *p;

				one.attribute = attrs[k];
				one.have_attribute = true;
				put_report(&w, srv, &one);
			}
			continue;
		}

		put_report(&w, srv, p);
	}

	(void)matter_tlv_end_container(&w);
	/* Read, not Subscribe: reporting/Engine.cpp:834-836. */
	(void)matter_tlv_put_bool(&w, MATTER_TLV_CTX(TAG_REPORT_SUPPRESS_RESPONSE), true);
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_IM_REVISION), MATTER_IM_REVISION);
	(void)matter_tlv_end_container(&w);

	return matter_tlv_writer_finish(&w, out_len);
}

/* ------------------------------------------------------------- invoke --- */

/** Decode one CommandPathIB. The reader is positioned ON the list element. */
static int decode_command_path(struct matter_tlv_reader *r, struct matter_im_invoke *inv)
{
	bool have_endpoint = false;
	bool have_cluster = false;
	bool have_command = false;
	int rc;

	rc = matter_tlv_enter(r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		uint64_t v;

		rc = matter_tlv_next(r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}
		if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDPATH_ENDPOINT)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT16_MAX) {
				return MATTER_E_INVAL;
			}
			inv->endpoint = (uint16_t)v;
			have_endpoint = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDPATH_CLUSTER)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT32_MAX) {
				return MATTER_E_INVAL;
			}
			inv->cluster = (uint32_t)v;
			have_cluster = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDPATH_COMMAND)) {
			rc = matter_tlv_get_u64(r, &v);
			if (rc != MATTER_OK || v > UINT32_MAX) {
				return MATTER_E_INVAL;
			}
			inv->command = (uint32_t)v;
			have_command = true;
		}
	}

	/*
	 * A command path has no wildcards. Unlike a read, which may legitimately
	 * ask about a whole cluster, invoking "some command somewhere" is not a
	 * thing the spec defines, so a missing field is a malformed message
	 * rather than a broad request.
	 */
	if (!have_endpoint || !have_cluster || !have_command) {
		return MATTER_E_INVAL;
	}
	return matter_tlv_exit(r);
}

/** Decode one CommandDataIB. The reader is positioned ON the structure. */
static int decode_command_data(struct matter_tlv_reader *r, struct matter_im_invoke *inv)
{
	bool have_path = false;
	int rc;

	rc = matter_tlv_enter(r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		/*
		 * Where the element about to be read STARTS. next_off is what the
		 * next scan begins at, so recording it before next() is the only
		 * way to bound a whole element: end_off stops at a container's
		 * head and says nothing about its body (matter_tlv.h:184-185).
		 */
		size_t elem_start = r->next_off;

		rc = matter_tlv_next(r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDDATA_PATH)) {
			if (matter_tlv_element_type(r) != MATTER_TLV_LIST) {
				return MATTER_E_TYPE;
			}
			rc = decode_command_path(r, inv);
			if (rc != MATTER_OK) {
				return rc;
			}
			have_path = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDDATA_FIELDS)) {
			/*
			 * Borrowed, not copied, and deliberately left undecoded:
			 * what the fields MEAN depends on which command this is,
			 * which is the cluster's business rather than the message
			 * layer's. Handed over as the COMPLETE element -- tag,
			 * body and end marker -- so a reader can be pointed at it
			 * directly.
			 *
			 * Valid only as long as the caller's buffer is.
			 */
			if (!matter_tlv_is_container(r)) {
				return MATTER_E_TYPE;
			}
			rc = matter_tlv_enter(r);
			if (rc != MATTER_OK) {
				return rc;
			}
			rc = matter_tlv_exit(r);
			if (rc != MATTER_OK) {
				return rc;
			}
			inv->fields = r->buf + elem_start;
			inv->fields_len = (size_t)(r->next_off - elem_start);
			inv->has_fields = true;
		} else if (matter_tlv_tag(r) == MATTER_TLV_CTX(TAG_CMDDATA_REF)) {
			uint64_t v;

			if (matter_tlv_get_u64(r, &v) != MATTER_OK) {
				return MATTER_E_INVAL;
			}
			inv->command_ref = v;
			inv->has_command_ref = true;
		}
	}

	if (!have_path) {
		return MATTER_E_INVAL;
	}
	return matter_tlv_exit(r);
}

int matter_im_invoke_request_decode(const uint8_t *tlv, size_t len, struct matter_im_invoke *out)
{
	struct matter_tlv_reader r;
	unsigned int seen = 0u;
	int rc;

	if (tlv == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}

	memset(out, 0, sizeof(*out));
	matter_tlv_reader_init(&r, tlv, len);

	rc = matter_tlv_next(&r);
	if (rc != MATTER_OK) {
		return (rc == MATTER_END) ? MATTER_E_INVAL : rc;
	}
	if (matter_tlv_element_type(&r) != MATTER_TLV_STRUCTURE) {
		return MATTER_E_TYPE;
	}
	rc = matter_tlv_enter(&r);
	if (rc != MATTER_OK) {
		return rc;
	}

	for (;;) {
		rc = matter_tlv_next(&r);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}

		if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_INVOKE_SUPPRESS_RESPONSE)) {
			if (matter_tlv_get_bool(&r, &out->suppress_response) != MATTER_OK) {
				return MATTER_E_INVAL;
			}
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_INVOKE_TIMED_REQUEST)) {
			if (matter_tlv_get_bool(&r, &out->timed_request) != MATTER_OK) {
				return MATTER_E_INVAL;
			}
		} else if (matter_tlv_tag(&r) == MATTER_TLV_CTX(TAG_INVOKE_REQUESTS)) {
			if (matter_tlv_element_type(&r) != MATTER_TLV_ARRAY) {
				return MATTER_E_TYPE;
			}
			rc = matter_tlv_enter(&r);
			if (rc != MATTER_OK) {
				return rc;
			}
			for (;;) {
				rc = matter_tlv_next(&r);
				if (rc == MATTER_END) {
					break;
				}
				if (rc != MATTER_OK) {
					return rc;
				}
				if (matter_tlv_element_type(&r) != MATTER_TLV_STRUCTURE) {
					return MATTER_E_TYPE;
				}
				seen++;
				if (seen > 1u) {
					return MATTER_E_NOSPACE;
				}
				rc = decode_command_data(&r, out);
				if (rc != MATTER_OK) {
					return rc;
				}
			}
			rc = matter_tlv_exit(&r);
			if (rc != MATTER_OK) {
				return rc;
			}
		}
	}

	if (seen != 1u) {
		return MATTER_E_INVAL;
	}
	return matter_tlv_exit(&r);
}

/** Write one CommandPathIB naming @p command on the invoked path. */
static void put_command_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag,
			     const struct matter_im_invoke *inv, uint32_t command)
{
	(void)matter_tlv_start_container(w, tag, MATTER_TLV_LIST);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CMDPATH_ENDPOINT), inv->endpoint);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CMDPATH_CLUSTER), inv->cluster);
	(void)matter_tlv_put_u64(w, MATTER_TLV_CTX(TAG_CMDPATH_COMMAND), command);
	(void)matter_tlv_end_container(w);
}

int matter_im_invoke_response_encode(const struct matter_im_server *srv,
				     const struct matter_im_invoke *inv, uint8_t *out, size_t cap,
				     size_t *out_len)
{
	struct matter_tlv_writer w;
	uint32_t response_command = MATTER_IM_NO_RESPONSE;
	uint8_t status;

	if (srv == NULL || srv->command == NULL || srv->command_fields == NULL || inv == NULL ||
	    out == NULL || out_len == NULL) {
		return MATTER_E_INVAL;
	}

	/*
	 * Run it FIRST, and run it whatever the peer said about responses: the
	 * commissioner asked for the effect, and SuppressResponse only says it
	 * does not want to be told about it.
	 */
	status = srv->command(srv->ctx, inv, &response_command);

	if (inv->suppress_response) {
		*out_len = 0u;
		return MATTER_OK;
	}

	matter_tlv_writer_init(&w, out, cap);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_CTX(TAG_IRESP_SUPPRESS_RESPONSE), false);
	(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_IRESP_RESPONSES), MATTER_TLV_ARRAY);
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);

	if (status == MATTER_IM_STATUS_SUCCESS && response_command != MATTER_IM_NO_RESPONSE) {
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_IRESPIB_COMMAND),
						 MATTER_TLV_STRUCTURE);
		put_command_path(&w, MATTER_TLV_CTX(TAG_CMDDATA_PATH), inv, response_command);
		srv->command_fields(srv->ctx, inv->endpoint, inv->cluster, response_command, &w,
				    MATTER_TLV_CTX(TAG_CMDDATA_FIELDS));
		if (inv->has_command_ref) {
			(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_CMDDATA_REF),
						 inv->command_ref);
		}
		(void)matter_tlv_end_container(&w);
	} else {
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_IRESPIB_STATUS),
						 MATTER_TLV_STRUCTURE);
		/* The path echoes the command that was ASKED for. There is no
		 * response command to name here, whether because nothing ran or
		 * because what ran had nothing to report. */
		put_command_path(&w, MATTER_TLV_CTX(TAG_CMDSTATUS_PATH), inv, inv->command);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(TAG_CMDSTATUS_STATUS),
						 MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_STATUS_STATUS), status);
		(void)matter_tlv_end_container(&w);
		if (inv->has_command_ref) {
			(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_CMDSTATUS_REF),
						 inv->command_ref);
		}
		(void)matter_tlv_end_container(&w);
	}

	(void)matter_tlv_end_container(&w);
	(void)matter_tlv_end_container(&w);
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(TAG_IM_REVISION), MATTER_IM_REVISION);
	(void)matter_tlv_end_container(&w);

	return matter_tlv_writer_finish(&w, out_len);
}
