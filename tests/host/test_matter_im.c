/**
 * @file test_matter_im.c — the read a commissioner actually sends.
 *
 * The request replayed here is not constructed and not invented. It is the 106
 * bytes a real iPhone sent this node immediately after PASE completed, lifted
 * from the device log. Encoding a request by hand would only prove this code
 * agrees with itself about the format; these bytes prove it agrees with Apple.
 *
 * Safe to keep in the tree: an attribute path list names cluster and attribute
 * numbers and nothing else. No node ids, no addresses, no key material -- the
 * message header that carried them was stripped before logging.
 *
 * Responses are checked by DECODING them again rather than by comparing against
 * a golden blob. A golden blob locks in whatever this code happened to emit the
 * day it was written, including its mistakes; decoding asserts the properties
 * that actually matter, and says which one broke.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>

#include "matter_clusters.h"
#include "matter_im.h"

#include "test.h"

/*
 * ReadRequestMessage, protocol 0x0001 opcode 0x02, as received.
 *
 *   endpoint 0  cluster 0x0030  attributes 0x04 0x00 0x01 0x02 0x03 0x0C
 *   endpoint 0  cluster 0x0028  attributes 0x02 0x04
 *   endpoint 0  cluster 0x0038  ALL attributes (wildcard)
 *   FabricFiltered false, InteractionModelRevision 12
 */
static const uint8_t apple_read[] = {
	0x15, 0x36, 0x00, 0x17, 0x24, 0x02, 0x00, 0x24, 0x03, 0x30, 0x24, 0x04, 0x04, 0x18,
	0x17, 0x24, 0x02, 0x00, 0x24, 0x03, 0x30, 0x24, 0x04, 0x00, 0x18, 0x17, 0x24, 0x02,
	0x00, 0x24, 0x03, 0x30, 0x24, 0x04, 0x01, 0x18, 0x17, 0x24, 0x02, 0x00, 0x24, 0x03,
	0x30, 0x24, 0x04, 0x02, 0x18, 0x17, 0x24, 0x02, 0x00, 0x24, 0x03, 0x30, 0x24, 0x04,
	0x03, 0x18, 0x17, 0x24, 0x02, 0x00, 0x24, 0x03, 0x30, 0x24, 0x04, 0x0c, 0x18, 0x17,
	0x24, 0x02, 0x00, 0x24, 0x03, 0x28, 0x24, 0x04, 0x02, 0x18, 0x17, 0x24, 0x02, 0x00,
	0x24, 0x03, 0x28, 0x24, 0x04, 0x04, 0x18, 0x17, 0x24, 0x02, 0x00, 0x24, 0x03, 0x38,
	0x18, 0x18, 0x28, 0x03, 0x24, 0xff, 0x0c, 0x18,
};

/** One decoded AttributeReportIB, flattened for assertion. */
struct rep {
	uint16_t endpoint;
	uint32_t cluster;
	uint32_t attribute;
	bool is_status;
	uint8_t status;
	uint8_t vtype; /* wire element type of the value */
	uint64_t vu;
	bool vb;
	/* BasicCommissioningInfo's two fields, when the value is a structure. */
	uint64_t s0;
	uint64_t s1;
};

#define MAX_REPS 16

/** Read an AttributePathIB into @p r. Reader is positioned on the list. */
static int walk_path(struct matter_tlv_reader *rd, struct rep *r)
{
	int rc = matter_tlv_enter(rd);

	if (rc != MATTER_OK) {
		return rc;
	}
	for (;;) {
		uint64_t v;

		rc = matter_tlv_next(rd);
		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return rc;
		}
		if (matter_tlv_get_u64(rd, &v) != MATTER_OK) {
			continue;
		}
		if (matter_tlv_tag(rd) == MATTER_TLV_CTX(2)) {
			r->endpoint = (uint16_t)v;
		} else if (matter_tlv_tag(rd) == MATTER_TLV_CTX(3)) {
			r->cluster = (uint32_t)v;
		} else if (matter_tlv_tag(rd) == MATTER_TLV_CTX(4)) {
			r->attribute = (uint32_t)v;
		}
	}
	return matter_tlv_exit(rd);
}

/** Decode a whole ReportData into @p reps. @return count, or -1. */
static int walk_report(const uint8_t *buf, size_t len, struct rep *reps, bool *suppress,
		       uint64_t *revision)
{
	struct matter_tlv_reader rd;
	int n = 0;

	*suppress = false;
	*revision = 0u;
	matter_tlv_reader_init(&rd, buf, len);

	if (matter_tlv_next(&rd) != MATTER_OK || matter_tlv_enter(&rd) != MATTER_OK) {
		return -1;
	}

	for (;;) {
		int rc = matter_tlv_next(&rd);

		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return -1;
		}

		if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(4)) {
			if (matter_tlv_get_bool(&rd, suppress) != MATTER_OK) {
				return -1;
			}
			continue;
		}
		if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0xFF)) {
			if (matter_tlv_get_u64(&rd, revision) != MATTER_OK) {
				return -1;
			}
			continue;
		}
		if (matter_tlv_tag(&rd) != MATTER_TLV_CTX(1)) {
			continue;
		}

		/* The AttributeReportIBs array. */
		if (matter_tlv_enter(&rd) != MATTER_OK) {
			return -1;
		}
		for (;;) {
			struct rep *r;

			rc = matter_tlv_next(&rd);
			if (rc == MATTER_END) {
				break;
			}
			if (rc != MATTER_OK || n >= MAX_REPS) {
				return -1;
			}
			r = &reps[n];
			memset(r, 0, sizeof(*r));

			/* AttributeReportIB: one anonymous structure. */
			if (matter_tlv_enter(&rd) != MATTER_OK) {
				return -1;
			}
			if (matter_tlv_next(&rd) != MATTER_OK) {
				return -1;
			}
			r->is_status = (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0));

			/* AttributeStatusIB or AttributeDataIB. */
			if (matter_tlv_enter(&rd) != MATTER_OK) {
				return -1;
			}
			for (;;) {
				rc = matter_tlv_next(&rd);
				if (rc == MATTER_END) {
					break;
				}
				if (rc != MATTER_OK) {
					return -1;
				}

				if (r->is_status) {
					if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0)) {
						if (walk_path(&rd, r) != MATTER_OK) {
							return -1;
						}
					} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1)) {
						/* StatusIB */
						uint64_t s = 0u;

						if (matter_tlv_enter(&rd) != MATTER_OK ||
						    matter_tlv_next(&rd) != MATTER_OK ||
						    matter_tlv_get_u64(&rd, &s) != MATTER_OK) {
							return -1;
						}
						r->status = (uint8_t)s;
						if (matter_tlv_exit(&rd) != MATTER_OK) {
							return -1;
						}
					}
					continue;
				}

				if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1)) {
					if (walk_path(&rd, r) != MATTER_OK) {
						return -1;
					}
				} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(2)) {
					r->vtype = matter_tlv_element_type(&rd);
					if (r->vtype == MATTER_TLV_STRUCTURE) {
						if (matter_tlv_enter(&rd) != MATTER_OK) {
							return -1;
						}
						if (matter_tlv_next(&rd) != MATTER_OK ||
						    matter_tlv_get_u64(&rd, &r->s0) != MATTER_OK) {
							return -1;
						}
						if (matter_tlv_next(&rd) != MATTER_OK ||
						    matter_tlv_get_u64(&rd, &r->s1) != MATTER_OK) {
							return -1;
						}
						if (matter_tlv_exit(&rd) != MATTER_OK) {
							return -1;
						}
					} else if (matter_tlv_get_bool(&rd, &r->vb) == MATTER_OK) {
						r->vb = r->vb;
					} else if (matter_tlv_get_u64(&rd, &r->vu) != MATTER_OK) {
						return -1;
					}
				}
			}
			if (matter_tlv_exit(&rd) != MATTER_OK) {
				return -1;
			}
			if (matter_tlv_exit(&rd) != MATTER_OK) {
				return -1;
			}
			n++;
		}
		if (matter_tlv_exit(&rd) != MATTER_OK) {
			return -1;
		}
	}
	return n;
}

/** Find the report for one path; NULL when it was omitted entirely. */
static const struct rep *find(const struct rep *reps, int n, uint32_t cluster, uint32_t attribute)
{
	for (int i = 0; i < n; i++) {
		if (reps[i].cluster == cluster && reps[i].attribute == attribute) {
			return &reps[i];
		}
	}
	return NULL;
}

static void fill_info(struct matter_device_info *info)
{
	memset(info, 0, sizeof(*info));
	info->vendor_id = 0xFFF1u;
	info->product_id = 0x8001u;
	info->breadcrumb = 0u;
	info->regulatory_config = MATTER_REGULATORY_INDOOR;
	info->location_capability = MATTER_REGULATORY_INDOOR;
	info->failsafe_expiry_s = 60u;
	info->failsafe_max_s = 900u;
	info->supports_concurrent_connection = true;
}

void test_matter_im(void)
{
	struct matter_im_read req;
	struct matter_device_info info;
	struct matter_im_server srv;
	struct matter_im_report_stats stats;
	struct rep reps[MAX_REPS];
	uint8_t out[512];
	size_t len = 0u;
	bool suppress = false;
	uint64_t revision = 0u;
	const struct rep *r;
	int n;

	/* ------------------------------------------------ decoding the read --- */

	T_EQ("apple read decodes",
	     matter_im_read_request_decode(apple_read, sizeof(apple_read), &req), MATTER_OK);
	T_EQ("nine paths", req.n_paths, 9);
	T_OK("not fabric filtered", !req.fabric_filtered);

	/* First path: GeneralCommissioning SupportsConcurrentConnection. */
	T_EQ("path0 endpoint", req.paths[0].endpoint, 0);
	T_EQ("path0 cluster", req.paths[0].cluster, 0x0030);
	T_EQ("path0 attribute", req.paths[0].attribute, 0x0004);
	T_OK("path0 concrete", !matter_im_path_is_wildcard(&req.paths[0]));

	/* Sixth: the attribute this node does not implement. */
	T_EQ("path5 attribute", req.paths[5].attribute, 0x000C);

	/* Seventh and eighth: BasicInformation. */
	T_EQ("path6 cluster", req.paths[6].cluster, 0x0028);
	T_EQ("path6 attribute", req.paths[6].attribute, 0x0002);
	T_EQ("path7 attribute", req.paths[7].attribute, 0x0004);

	/* Ninth: cluster given, attribute wildcarded. */
	T_EQ("path8 cluster", req.paths[8].cluster, 0x0038);
	T_OK("path8 has endpoint", req.paths[8].have_endpoint);
	T_OK("path8 has cluster", req.paths[8].have_cluster);
	T_OK("path8 attribute wildcarded", !req.paths[8].have_attribute);
	T_OK("path8 is a wildcard path", matter_im_path_is_wildcard(&req.paths[8]));

	/* ------------------------------------------------ answering the read --- */

	fill_info(&info);
	matter_clusters_init(&srv, &info);

	T_EQ("report encodes",
	     matter_im_report_data_encode(&srv, &req, out, sizeof(out), &len, &stats), MATTER_OK);
	T_OK("report is not empty", len > 0u);
	/* The TimeSynchronization wildcard: skipped because the cluster is absent,
	 * which is the CORRECT answer and must not be an error. */
	T_EQ("one wildcard skipped", stats.skipped_wildcard, 1);
	T_EQ("nothing left unexpanded", stats.unexpanded_wildcard, 0);

	n = walk_report(out, len, reps, &suppress, &revision);
	/* Eight concrete paths answered; the wildcard contributes nothing. */
	T_EQ("eight reports", n, 8);
	T_OK("suppress response set", suppress);
	T_EQ("interaction model revision", (long)revision, MATTER_IM_REVISION);

	r = find(reps, n, 0x0028, 0x0002);
	T_OK("vendor id reported", r != NULL && !r->is_status);
	T_EQ("vendor id value", r ? (long)r->vu : -1, 0xFFF1);
	T_EQ("vendor id endpoint", r ? r->endpoint : 0xFFFF, 0);

	r = find(reps, n, 0x0028, 0x0004);
	T_OK("product id reported", r != NULL && !r->is_status);
	T_EQ("product id value", r ? (long)r->vu : -1, 0x8001);

	r = find(reps, n, 0x0030, 0x0000);
	T_OK("breadcrumb reported", r != NULL && !r->is_status);
	T_EQ("breadcrumb value", r ? (long)r->vu : -1, 0);

	r = find(reps, n, 0x0030, 0x0001);
	T_OK("basic commissioning info reported", r != NULL && !r->is_status);
	T_EQ("bci is a structure", r ? r->vtype : 0, MATTER_TLV_STRUCTURE);
	T_EQ("bci failsafe expiry", r ? (long)r->s0 : -1, 60);
	T_EQ("bci failsafe max", r ? (long)r->s1 : -1, 900);

	r = find(reps, n, 0x0030, 0x0002);
	T_OK("regulatory config reported", r != NULL && !r->is_status);
	T_EQ("regulatory config value", r ? (long)r->vu : -1, MATTER_REGULATORY_INDOOR);

	r = find(reps, n, 0x0030, 0x0003);
	T_OK("location capability reported", r != NULL && !r->is_status);

	r = find(reps, n, 0x0030, 0x0004);
	T_OK("concurrent connection reported", r != NULL && !r->is_status);
	T_OK("concurrent connection true", r != NULL && r->vb);

	/* The asymmetry, stated as a test: a CONCRETE path naming an attribute
	 * this node lacks gets a status, where the wildcard above got silence. */
	r = find(reps, n, 0x0030, 0x000C);
	T_OK("unimplemented attribute answered", r != NULL);
	T_OK("answered with a status", r != NULL && r->is_status);
	T_EQ("status is unsupported attribute", r ? r->status : 0,
	     MATTER_IM_STATUS_UNSUPPORTED_ATTRIBUTE);

	/* And the wildcard genuinely produced nothing at all. */
	T_OK("wildcard cluster absent from report", find(reps, n, 0x0038, 0) == NULL);

	/* ---------------------------------------------------- status choices --- */
	{
		struct matter_im_read one;
		struct rep sreps[MAX_REPS];
		int m;

		/* Unknown cluster on a known endpoint: UNSUPPORTED_CLUSTER, not
		 * UNSUPPORTED_ATTRIBUTE. MetadataLookup.cpp:68-88 reports the
		 * outermost missing thing, and a device that says "attribute" of
		 * a cluster it does not have is lying about having the cluster. */
		memset(&one, 0, sizeof(one));
		one.n_paths = 1;
		one.paths[0].endpoint = 0;
		one.paths[0].cluster = 0x0101; /* DoorLock, not implemented yet */
		one.paths[0].attribute = 0x0000;
		one.paths[0].have_endpoint = true;
		one.paths[0].have_cluster = true;
		one.paths[0].have_attribute = true;
		T_EQ("unknown cluster encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		m = walk_report(out, len, sreps, &suppress, &revision);
		T_EQ("one report", m, 1);
		T_OK("is a status", sreps[0].is_status);
		T_EQ("unsupported cluster", sreps[0].status, MATTER_IM_STATUS_UNSUPPORTED_CLUSTER);

		/* Unknown endpoint outranks both. */
		one.paths[0].endpoint = 1;
		one.paths[0].cluster = 0x0028;
		T_EQ("unknown endpoint encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		m = walk_report(out, len, sreps, &suppress, &revision);
		T_EQ("one report for bad endpoint", m, 1);
		T_EQ("unsupported endpoint", sreps[0].status,
		     MATTER_IM_STATUS_UNSUPPORTED_ENDPOINT);

		/* A wildcard over a cluster this node HAS expands to every
		 * attribute of it. It used to be skipped and counted, which was
		 * honest but incomplete -- two of Apple's three commissioning
		 * reads carry one, and both came back short. */
		one.paths[0].endpoint = 0;
		one.paths[0].cluster = 0x0028;
		one.paths[0].have_attribute = false;
		T_EQ("known-cluster wildcard encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		T_EQ("nothing left unexpanded", stats.unexpanded_wildcard, 0);
		T_EQ("nothing counted as absent", stats.skipped_wildcard, 0);
		m = walk_report(out, len, sreps, &suppress, &revision);
		T_EQ("both BasicInformation attributes reported", m, 2);

		/* An ENDPOINT wildcard still is not expanded: that would need this
		 * node to enumerate its own endpoints, which it does not do. It is
		 * counted rather than passed over in silence. */
		one.paths[0].have_endpoint = false;
		one.paths[0].have_attribute = true;
		one.paths[0].attribute = MATTER_ATTR_BASIC_VENDOR_ID;
		T_EQ("endpoint wildcard encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		T_EQ("counted as unexpanded", stats.unexpanded_wildcard, 1);
		m = walk_report(out, len, sreps, &suppress, &revision);
		T_EQ("no reports", m, 0);
		one.paths[0].have_endpoint = true;
	}

	/* --------------------------------------------------------- refusals --- */

	T_EQ("null request refused", matter_im_read_request_decode(NULL, 4, &req), MATTER_E_INVAL);
	T_EQ("null out refused",
	     matter_im_read_request_decode(apple_read, sizeof(apple_read), NULL), MATTER_E_INVAL);
	T_EQ("empty refused", matter_im_read_request_decode(apple_read, 0u, &req), MATTER_E_INVAL);

	/* Truncation must not be mistaken for a shorter request. Every prefix of
	 * a real message is malformed, and none may decode as if complete. */
	for (size_t cut = 1u; cut < sizeof(apple_read); cut++) {
		struct matter_im_read partial;

		if (matter_im_read_request_decode(apple_read, cut, &partial) == MATTER_OK) {
			T_OK("truncated request must not decode", false);
			break;
		}
	}
	T_OK("every truncation refused", true);

	/* A payload that is not a structure at all. */
	{
		static const uint8_t not_a_struct[] = {0x24, 0x00, 0x01};

		T_EQ("non-structure refused",
		     matter_im_read_request_decode(not_a_struct, sizeof(not_a_struct), &req),
		     MATTER_E_TYPE);
	}

	/* More paths than the bound: refused outright rather than truncated,
	 * because a silently shortened answer looks complete to the peer. */
	{
		uint8_t big[512];
		struct matter_tlv_writer w;
		size_t blen = 0u;

		matter_tlv_writer_init(&w, big, sizeof(big));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(0), MATTER_TLV_ARRAY);
		for (int i = 0; i < MATTER_IM_MAX_PATHS + 1; i++) {
			(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_LIST);
			(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2), 0u);
			(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(3), 0x0028u);
			(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(4), (uint64_t)i);
			(void)matter_tlv_end_container(&w);
		}
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		T_EQ("oversized request builds", matter_tlv_writer_finish(&w, &blen), MATTER_OK);
		T_EQ("too many paths refused", matter_im_read_request_decode(big, blen, &req),
		     MATTER_E_NOSPACE);
	}

	/* No room for the answer must fail rather than truncate: a short report
	 * is indistinguishable from a complete one once it reaches the peer. */
	T_EQ("cramped buffer refused",
	     matter_im_report_data_encode(&srv, &req, out, 8u, &len, &stats), MATTER_E_NOSPACE);
	T_EQ("null server refused",
	     matter_im_report_data_encode(NULL, &req, out, sizeof(out), &len, &stats),
	     MATTER_E_INVAL);
	{
		struct matter_im_server broken = srv;

		broken.value = NULL;
		T_EQ("incomplete server refused",
		     matter_im_report_data_encode(&broken, &req, out, sizeof(out), &len, &stats),
		     MATTER_E_INVAL);
	}

	/* stats is optional. */
	fill_info(&info);
	T_EQ("null stats accepted",
	     matter_im_report_data_encode(&srv, &req, out, sizeof(out), &len, NULL), MATTER_OK);

	/* ------------------------------------------------ wildcard expansion --- */
	{
		struct matter_im_read one;
		struct rep wreps[MAX_REPS];
		int m;

		/* A wildcard over a cluster this node HAS must now report every
		 * attribute of it, not skip the path. Apple sends these: two of
		 * its three commissioning reads carried them. */
		memset(&one, 0, sizeof(one));
		one.n_paths = 1;
		one.paths[0].endpoint = 0;
		one.paths[0].cluster = MATTER_CLUSTER_GENERAL_COMMISSIONING;
		one.paths[0].have_endpoint = true;
		one.paths[0].have_cluster = true;
		one.paths[0].have_attribute = false;

		fill_info(&info);
		T_EQ("wildcard encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		T_EQ("nothing left unexpanded", stats.unexpanded_wildcard, 0);
		T_EQ("nothing skipped", stats.skipped_wildcard, 0);
		m = walk_report(out, len, wreps, &suppress, &revision);
		T_EQ("all five attributes reported", m, 5);
		T_OK("breadcrumb among them", find(wreps, m, MATTER_CLUSTER_GENERAL_COMMISSIONING,
						   MATTER_ATTR_GC_BREADCRUMB) != NULL);
		T_OK("concurrent connection among them",
		     find(wreps, m, MATTER_CLUSTER_GENERAL_COMMISSIONING,
			  MATTER_ATTR_GC_SUPPORTS_CONCURRENT_CONNECTION) != NULL);
		/* Every expanded path must carry a VALUE. An expansion that
		 * yields UNSUPPORTED_ATTRIBUTE means the attribute list and the
		 * status function disagree, which is worse than not expanding. */
		for (int i = 0; i < m; i++) {
			T_OK("expansion yields values, not statuses", !wreps[i].is_status);
		}

		/* An absent cluster still expands to silence, not to an error. */
		one.paths[0].cluster = 0x0038u;
		T_EQ("absent wildcard encodes",
		     matter_im_report_data_encode(&srv, &one, out, sizeof(out), &len, &stats),
		     MATTER_OK);
		T_EQ("skipped", stats.skipped_wildcard, 1);
		T_EQ("not unexpanded", stats.unexpanded_wildcard, 0);
		m = walk_report(out, len, wreps, &suppress, &revision);
		T_EQ("no reports", m, 0);
	}
}

/*
 * InvokeRequestMessage carrying ArmFailSafe, exactly as a real iPhone sent it
 * once the three commissioning reads were answered.
 *
 *   SuppressResponse false, TimedRequest false
 *   endpoint 0, cluster 0x0030 GeneralCommissioning, command 0x00 ArmFailSafe
 *   ExpiryLengthSeconds 60, Breadcrumb 3
 */
static const uint8_t apple_armfailsafe[] = {
	0x15, 0x28, 0x00, 0x28, 0x01, 0x36, 0x02, 0x15, 0x37, 0x00, 0x24, 0x00,
	0x00, 0x24, 0x01, 0x30, 0x24, 0x02, 0x00, 0x18, 0x35, 0x01, 0x24, 0x00,
	0x3c, 0x24, 0x01, 0x03, 0x18, 0x18, 0x18, 0x24, 0xff, 0x0c, 0x18,
};

/** One decoded InvokeResponseIB. */
struct iresp {
	bool is_status;
	uint8_t status;
	uint16_t endpoint;
	uint32_t cluster;
	uint32_t command;
	uint64_t error_code;
	bool have_debug_text;
};

/** Decode an InvokeResponseMessage. @return true on success. */
static bool walk_invoke_response(const uint8_t *buf, size_t len, struct iresp *ir)
{
	struct matter_tlv_reader rd;

	memset(ir, 0, sizeof(*ir));
	matter_tlv_reader_init(&rd, buf, len);
	if (matter_tlv_next(&rd) != MATTER_OK || matter_tlv_enter(&rd) != MATTER_OK) {
		return false;
	}
	for (;;) {
		int rc = matter_tlv_next(&rd);

		if (rc == MATTER_END) {
			break;
		}
		if (rc != MATTER_OK) {
			return false;
		}
		if (matter_tlv_tag(&rd) != MATTER_TLV_CTX(1)) {
			continue;
		}
		/* InvokeResponses array. */
		if (matter_tlv_enter(&rd) != MATTER_OK) {
			return false;
		}
		if (matter_tlv_next(&rd) != MATTER_OK) {
			return false;
		}
		/* One InvokeResponseIB. */
		if (matter_tlv_enter(&rd) != MATTER_OK || matter_tlv_next(&rd) != MATTER_OK) {
			return false;
		}
		ir->is_status = (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1));
		if (matter_tlv_enter(&rd) != MATTER_OK) {
			return false;
		}
		for (;;) {
			rc = matter_tlv_next(&rd);
			if (rc == MATTER_END) {
				break;
			}
			if (rc != MATTER_OK) {
				return false;
			}
			if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0)) {
				/* CommandPathIB, a list. */
				uint64_t v;

				if (matter_tlv_enter(&rd) != MATTER_OK) {
					return false;
				}
				for (;;) {
					rc = matter_tlv_next(&rd);
					if (rc == MATTER_END) {
						break;
					}
					if (rc != MATTER_OK ||
					    matter_tlv_get_u64(&rd, &v) != MATTER_OK) {
						return false;
					}
					if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0)) {
						ir->endpoint = (uint16_t)v;
					} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1)) {
						ir->cluster = (uint32_t)v;
					} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(2)) {
						ir->command = (uint32_t)v;
					}
				}
				if (matter_tlv_exit(&rd) != MATTER_OK) {
					return false;
				}
			} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1)) {
				/* Fields struct, or StatusIB. */
				if (matter_tlv_enter(&rd) != MATTER_OK) {
					return false;
				}
				for (;;) {
					rc = matter_tlv_next(&rd);
					if (rc == MATTER_END) {
						break;
					}
					if (rc != MATTER_OK) {
						return false;
					}
					if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(0)) {
						uint64_t v = 0u;

						if (matter_tlv_get_u64(&rd, &v) != MATTER_OK) {
							return false;
						}
						ir->error_code = v;
						if (ir->is_status) {
							ir->status = (uint8_t)v;
						}
					} else if (matter_tlv_tag(&rd) == MATTER_TLV_CTX(1)) {
						const char *sp = NULL;
						size_t sl = 0u;

						if (matter_tlv_get_utf8(&rd, &sp, &sl) ==
						    MATTER_OK) {
							ir->have_debug_text = true;
						}
					}
				}
				if (matter_tlv_exit(&rd) != MATTER_OK) {
					return false;
				}
			}
		}
		return true;
	}
	return false;
}

void test_matter_im_invoke(void)
{
	struct matter_im_invoke inv;
	struct matter_device_info info;
	struct matter_im_server srv;
	struct iresp ir;
	uint8_t out[256];
	size_t len = 0u;

	fill_info(&info);
	matter_clusters_init(&srv, &info);

	t_group("ArmFailSafe, as a real iPhone sent it");
	{
		T_EQ("decodes",
		     matter_im_invoke_request_decode(apple_armfailsafe, sizeof(apple_armfailsafe),
						     &inv),
		     MATTER_OK);
		T_EQ("endpoint", inv.endpoint, 0);
		T_EQ("GeneralCommissioning", (long)inv.cluster, 0x0030L);
		T_EQ("ArmFailSafe", (long)inv.command, 0x0000L);
		T_OK("carries fields", inv.has_fields);
		T_OK("response wanted", !inv.suppress_response);
		T_OK("not timed", !inv.timed_request);
		T_OK("no command ref", !inv.has_command_ref);

		T_OK("fail-safe not armed yet", !info.failsafe_armed);
		T_EQ("encodes a response",
		     matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &len),
		     MATTER_OK);
		T_OK("response is not empty", len > 0u);
		/* The command RAN: the effect is the point, and the breadcrumb is
		 * how the commissioner resumes a half-finished attempt. */
		T_OK("fail-safe armed", info.failsafe_armed);
		T_EQ("breadcrumb taken from the request", (long)info.breadcrumb, 3L);

		T_OK("response decodes", walk_invoke_response(out, len, &ir));
		T_OK("carries a command, not a status", !ir.is_status);
		T_EQ("same endpoint", ir.endpoint, 0);
		T_EQ("same cluster", (long)ir.cluster, 0x0030L);
		/* The path names the RESPONSE command, not the one invoked. */
		T_EQ("ArmFailSafeResponse", (long)ir.command,
		     (long)MATTER_CMD_GC_ARM_FAIL_SAFE_RESPONSE);
		T_EQ("ErrorCode OK", (long)ir.error_code, (long)MATTER_COMMISSIONING_OK);
		/* DebugText is mandatory; omitting it fails the decode at the
		 * commissioner, which shows up as a hang rather than an error. */
		T_OK("DebugText present", ir.have_debug_text);
	}

	t_group("SetRegulatoryConfig");
	{
		uint8_t buf[64];
		struct matter_tlv_writer w;
		size_t blen = 0u;

		/* NewRegulatoryConfig indoor, CountryCode XX, Breadcrumb 5. */
		matter_tlv_writer_init(&w, buf, sizeof(buf));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_bool(&w, MATTER_TLV_CTX(0), false);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(2), MATTER_TLV_ARRAY);
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(0), MATTER_TLV_LIST);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), 0u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(1),
					 MATTER_CLUSTER_GENERAL_COMMISSIONING);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2),
					 MATTER_CMD_GC_SET_REGULATORY_CONFIG);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(1), MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), MATTER_REGULATORY_INDOOR);
		(void)matter_tlv_put_utf8(&w, MATTER_TLV_CTX(1), "XX", 2u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2), 5u);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		T_EQ("request builds", matter_tlv_writer_finish(&w, &blen), MATTER_OK);

		T_EQ("decodes", matter_im_invoke_request_decode(buf, blen, &inv), MATTER_OK);
		T_EQ("SetRegulatoryConfig", (long)inv.command,
		     (long)MATTER_CMD_GC_SET_REGULATORY_CONFIG);
		T_EQ("encodes",
		     matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &len),
		     MATTER_OK);
		T_OK("decodes", walk_invoke_response(out, len, &ir));
		T_EQ("SetRegulatoryConfigResponse", (long)ir.command,
		     (long)MATTER_CMD_GC_SET_REGULATORY_CONFIG_RESPONSE);
		T_EQ("accepted", (long)ir.error_code, (long)MATTER_COMMISSIONING_OK);
		T_EQ("breadcrumb advanced", (long)info.breadcrumb, 5L);

		/* A location this node never claimed is refused with a
		 * CommissioningError, not accepted silently. */
		buf[0] = buf[0]; /* rebuild with outdoor */
		matter_tlv_writer_init(&w, buf, sizeof(buf));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(2), MATTER_TLV_ARRAY);
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(0), MATTER_TLV_LIST);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), 0u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(1),
					 MATTER_CLUSTER_GENERAL_COMMISSIONING);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2),
					 MATTER_CMD_GC_SET_REGULATORY_CONFIG);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(1), MATTER_TLV_STRUCTURE);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), MATTER_REGULATORY_OUTDOOR);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_writer_finish(&w, &blen);
		T_EQ("decodes", matter_im_invoke_request_decode(buf, blen, &inv), MATTER_OK);
		T_EQ("encodes",
		     matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &len),
		     MATTER_OK);
		T_OK("decodes", walk_invoke_response(out, len, &ir));
		T_EQ("refused as outside range", (long)ir.error_code,
		     (long)MATTER_COMMISSIONING_VALUE_OUTSIDE_RANGE);
		T_EQ("and the config is unchanged", info.regulatory_config,
		     MATTER_REGULATORY_INDOOR);
	}

	t_group("commands this node does not have");
	{
		uint8_t buf[64];
		struct matter_tlv_writer w;
		size_t blen = 0u;

		matter_tlv_writer_init(&w, buf, sizeof(buf));
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(2), MATTER_TLV_ARRAY);
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
		(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(0), MATTER_TLV_LIST);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), 0u);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(1),
					 MATTER_CLUSTER_GENERAL_COMMISSIONING);
		(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2), 0x00FFu); /* no such command */
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_end_container(&w);
		(void)matter_tlv_writer_finish(&w, &blen);

		T_EQ("decodes", matter_im_invoke_request_decode(buf, blen, &inv), MATTER_OK);
		T_EQ("encodes",
		     matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &len),
		     MATTER_OK);
		T_OK("decodes", walk_invoke_response(out, len, &ir));
		T_OK("answered with a status", ir.is_status);
		T_EQ("unsupported command", ir.status, MATTER_IM_STATUS_UNSUPPORTED_COMMAND);
		/* The status path echoes the command ASKED for: there is no
		 * response command when nothing ran. */
		T_EQ("path names the invoked command", (long)ir.command, 0x00FFL);
	}

	t_group("what invoke refuses");
	{
		T_EQ("null refused", matter_im_invoke_request_decode(NULL, 4u, &inv),
		     MATTER_E_INVAL);
		T_EQ("empty refused", matter_im_invoke_request_decode(apple_armfailsafe, 0u, &inv),
		     MATTER_E_INVAL);

		/* Every truncation of a real request must be refused. */
		for (size_t cut = 1u; cut < sizeof(apple_armfailsafe); cut++) {
			if (matter_im_invoke_request_decode(apple_armfailsafe, cut, &inv) ==
			    MATTER_OK) {
				T_OK("truncated invoke must not decode", false);
				break;
			}
		}
		T_OK("every truncation refused", true);

		/* A batch is refused rather than half-run: a commissioner that
		 * batched two commands and got one response would be entitled to
		 * assume both happened. */
		{
			uint8_t buf[128];
			struct matter_tlv_writer w;
			size_t blen = 0u;

			matter_tlv_writer_init(&w, buf, sizeof(buf));
			(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
			(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(2), MATTER_TLV_ARRAY);
			for (int i = 0; i < 2; i++) {
				(void)matter_tlv_start_container(&w, MATTER_TLV_ANON,
								 MATTER_TLV_STRUCTURE);
				(void)matter_tlv_start_container(&w, MATTER_TLV_CTX(0),
								 MATTER_TLV_LIST);
				(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(0), 0u);
				(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(1), 0x0030u);
				(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(2), 0u);
				(void)matter_tlv_end_container(&w);
				(void)matter_tlv_end_container(&w);
			}
			(void)matter_tlv_end_container(&w);
			(void)matter_tlv_end_container(&w);
			(void)matter_tlv_writer_finish(&w, &blen);
			T_EQ("a batch is refused", matter_im_invoke_request_decode(buf, blen, &inv),
			     MATTER_E_NOSPACE);
		}

		/* No command at all. */
		{
			static const uint8_t empty_invoke[] = {0x15, 0x36, 0x02, 0x18, 0x18};

			T_EQ("no command refused",
			     matter_im_invoke_request_decode(empty_invoke, sizeof(empty_invoke),
							     &inv),
			     MATTER_E_INVAL);
		}
	}

	t_group("SuppressResponse runs the command and says nothing");
	{
		fill_info(&info);
		T_EQ("decodes",
		     matter_im_invoke_request_decode(apple_armfailsafe, sizeof(apple_armfailsafe),
						     &inv),
		     MATTER_OK);
		inv.suppress_response = true;
		len = 1u;
		T_EQ("encodes",
		     matter_im_invoke_response_encode(&srv, &inv, out, sizeof(out), &len),
		     MATTER_OK);
		T_EQ("nothing to send", (long)len, 0L);
		T_OK("but the command still ran", info.failsafe_armed);
	}
}
