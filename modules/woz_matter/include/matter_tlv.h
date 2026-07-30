/**
 * @file matter_tlv.h — Matter TLV encoder (Matter Core spec, Appendix A).
 *
 * This is NOT the BER/DER-TLV in modules/woz_aliro_stack/src/protocol/tlv.h.
 * Matter uses its own encoding: one control byte carrying a 3-bit tag control
 * and a 5-bit element type, then 0-8 tag octets, then the value, all
 * little-endian. The two share a name and nothing else, so they stay separate.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 1 of internal/cdk-matter-plan.md. Everything Apple Home sends after
 * commissioning -- SetAliroReaderConfig included -- arrives as Matter TLV, and
 * nothing in this repo could read a byte of it before this file.
 *
 * Constants here are not from memory. They were read out of the vendored CHIP
 * SDK at workspace/modules/lib/matter/src/lib/core/: element types from
 * TLVTypes.h:60-86, tag controls from TLVTags.h:105-112, and the special
 * profile IDs from TLVTags.h:74,97. The encoder is checked against CHIP's own
 * golden vectors (src/lib/core/tests/TestTLV.cpp Encoding2/Encoding3), which is
 * a stronger test than a round-trip: a codec that is wrong in a self-consistent
 * way still round-trips perfectly.
 *
 * A tag is one uint64_t, profile ID in the high 32 bits and tag number in the
 * low 32, because passing it in a register pair costs less than a struct on a
 * part where the system work queue has 528 B of headroom. (CHIP stores the
 * COMPLEMENT of the profile ID in its own Tag type, TLVTags.h:137. That is an
 * internal detail of theirs and is deliberately not copied -- the wire format
 * is what has to match, not the in-memory shape.)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Profile ID reserved for anonymous and context-specific tags (TLVTags.h:74). */
#define MATTER_TLV_SPECIAL_PROFILE 0xFFFFFFFFu
/** The Matter common profile (TLVTags.h:97). */
#define MATTER_TLV_COMMON_PROFILE 0x00000000u
/** One past kContextTagMaxNum, so it cannot collide with a context tag (TLVTags.h:34-35). */
#define MATTER_TLV_ANON_TAG_NUM 0x00000100u

typedef uint64_t matter_tlv_tag_t;

#define MATTER_TLV_TAG(profile, num)                                                               \
	(((matter_tlv_tag_t)(uint32_t)(profile) << 32) | (matter_tlv_tag_t)(uint32_t)(num))

/** Anonymous tag: no tag octets on the wire. */
#define MATTER_TLV_ANON MATTER_TLV_TAG(MATTER_TLV_SPECIAL_PROFILE, MATTER_TLV_ANON_TAG_NUM)
/** Context-specific tag, 0..255. The common case inside a structure. */
#define MATTER_TLV_CTX(n) MATTER_TLV_TAG(MATTER_TLV_SPECIAL_PROFILE, (uint8_t)(n))
/** Common-profile tag. */
#define MATTER_TLV_COMMON(n) MATTER_TLV_TAG(MATTER_TLV_COMMON_PROFILE, (n))
/** Explicit profile tag. Encodes implicit if it matches the writer's implicit profile. */
#define MATTER_TLV_PROFILE(profile, n) MATTER_TLV_TAG((profile), (n))

/** Container element types, values as on the wire (TLVTypes.h:83-85). */
#define MATTER_TLV_STRUCTURE 0x15u
#define MATTER_TLV_ARRAY     0x16u
#define MATTER_TLV_LIST      0x17u

enum matter_tlv_status {
	MATTER_TLV_OK = 0,
	/** Ran out of output buffer. */
	MATTER_TLV_E_NOSPACE = -1,
	/** Caller passed something the encoding cannot represent. */
	MATTER_TLV_E_INVAL = -2,
	/** Nesting past MATTER_TLV_MAX_DEPTH. */
	MATTER_TLV_E_DEPTH = -3,
	/** end_container with nothing open, or finish with one still open. */
	MATTER_TLV_E_STATE = -4,
};

/*
 * Bounded, and deliberately shallow. Matter's own structures nest ~4 deep; 8
 * leaves room without letting a peer drive nesting. The writer tracks depth
 * with a counter rather than recursion, which is the same reason the reader
 * will be iterative: stack depth must not be a function of peer input.
 */
#define MATTER_TLV_MAX_DEPTH 8

/**
 * Encoder state. Errors are STICKY: the first failure is latched into rc and
 * every later put becomes a no-op, so a long encode sequence is checked once at
 * matter_tlv_writer_finish() instead of after every call. That is the shape
 * that keeps call sites readable, and it cannot silently truncate -- finish()
 * returns the latched error.
 */
struct matter_tlv_writer {
	uint8_t *buf;
	size_t cap;
	size_t len;
	uint32_t implicit_profile;
	bool implicit_set;
	int rc;
	uint8_t depth;
};

/** Bind an output buffer. Always succeeds; a NULL buffer just latches E_INVAL on first use. */
void matter_tlv_writer_init(struct matter_tlv_writer *w, uint8_t *buf, size_t cap);

/**
 * Nominate a profile ID to encode with the shorter implicit tag control.
 * Without this every profile tag is written fully qualified.
 */
void matter_tlv_writer_set_implicit_profile(struct matter_tlv_writer *w, uint32_t profile);

int matter_tlv_put_bool(struct matter_tlv_writer *w, matter_tlv_tag_t tag, bool v);
int matter_tlv_put_null(struct matter_tlv_writer *w, matter_tlv_tag_t tag);

/** Signed integer, encoded in the smallest width that holds the value. */
int matter_tlv_put_i64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, int64_t v);
/** Unsigned integer, encoded in the smallest width that holds the value. */
int matter_tlv_put_u64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint64_t v);

/** UTF-8 string. Not validated as UTF-8; the caller owns that. */
int matter_tlv_put_utf8(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const char *s,
			size_t len);
int matter_tlv_put_bytes(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const uint8_t *b,
			 size_t len);

/** @param type one of MATTER_TLV_STRUCTURE, MATTER_TLV_ARRAY, MATTER_TLV_LIST. */
int matter_tlv_start_container(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type);
int matter_tlv_end_container(struct matter_tlv_writer *w);

/**
 * Close out the encoding.
 * @param out_len receives the encoded length on success; untouched on failure.
 * @return MATTER_TLV_OK, or the first error latched during encoding, or
 *         MATTER_TLV_E_STATE if a container is still open.
 */
int matter_tlv_writer_finish(struct matter_tlv_writer *w, size_t *out_len);

#ifdef __cplusplus
}
#endif
