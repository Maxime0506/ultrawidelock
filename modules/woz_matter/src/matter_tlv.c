/**
 * @file matter_tlv.c — Matter TLV encoder.
 *
 * Control byte = tag control (top 3 bits) | element type (bottom 5). Then the
 * tag octets, then the value. Everything multi-octet is little-endian.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Two decisions worth stating, because both are about the 528 B of headroom the
 * thread analyzer measured on this part's system work queue:
 *
 *   - No recursion anywhere. Depth is a counter.
 *   - No dynamic allocation. The caller owns the buffer.
 *
 * Integers are written in the smallest width that holds the value, which is
 * what the spec calls for and what CHIP's own encoder does, so its golden
 * vectors only match if this matches. Getting it wrong the other way (always
 * 8 octets) still decodes correctly everywhere and would pass a round-trip
 * test -- another reason the tests pin CHIP's bytes rather than round-tripping.
 */
#include <string.h>

#include "matter_tlv.h"

/* Tag controls, TLVTags.h:105-112. */
#define TC_ANON         0x00u
#define TC_CONTEXT      0x20u
#define TC_COMMON_2     0x40u
#define TC_COMMON_4     0x60u
#define TC_IMPLICIT_2   0x80u
#define TC_IMPLICIT_4   0xA0u
#define TC_FULLQUAL_6   0xC0u
#define TC_FULLQUAL_8   0xE0u

/* Element types, TLVTypes.h:60-86. */
#define ET_INT8          0x00u
#define ET_INT16         0x01u
#define ET_INT32         0x02u
#define ET_INT64         0x03u
#define ET_UINT8         0x04u
#define ET_UINT16        0x05u
#define ET_UINT32        0x06u
#define ET_UINT64        0x07u
#define ET_BOOL_FALSE    0x08u
#define ET_BOOL_TRUE     0x09u
#define ET_UTF8_LEN1     0x0Cu
#define ET_UTF8_LEN2     0x0Du
#define ET_UTF8_LEN4     0x0Eu
#define ET_UTF8_LEN8     0x0Fu
#define ET_BYTES_LEN1    0x10u
#define ET_BYTES_LEN2    0x11u
#define ET_BYTES_LEN4    0x12u
#define ET_BYTES_LEN8    0x13u
#define ET_NULL          0x14u
#define ET_END_CONTAINER 0x18u

static uint32_t tag_profile(matter_tlv_tag_t tag)
{
	return (uint32_t)(tag >> 32);
}

static uint32_t tag_number(matter_tlv_tag_t tag)
{
	return (uint32_t)(tag & 0xFFFFFFFFu);
}

/** Latch the first error and report whether the writer is still usable. */
static bool fail(struct matter_tlv_writer *w, int rc)
{
	if (w->rc == MATTER_TLV_OK) {
		w->rc = rc;
	}
	return false;
}

static bool room(struct matter_tlv_writer *w, size_t n)
{
	if (w->buf == NULL) {
		return fail(w, MATTER_TLV_E_INVAL);
	}
	if (n > w->cap - w->len) {
		return fail(w, MATTER_TLV_E_NOSPACE);
	}
	return true;
}

static void put_raw(struct matter_tlv_writer *w, const void *src, size_t n)
{
	memcpy(&w->buf[w->len], src, n);
	w->len += n;
}

/** Little-endian, n in 1/2/4/8. */
static void put_le(struct matter_tlv_writer *w, uint64_t v, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		w->buf[w->len + i] = (uint8_t)(v >> (8u * i));
	}
	w->len += n;
}

/**
 * Emit the control byte and tag octets.
 *
 * The tag control is chosen here, and the choice is where an encoder usually
 * goes wrong: a profile tag is written implicit only when it matches the
 * writer's nominated implicit profile, common when the profile is 0, and fully
 * qualified otherwise. A fully-qualified tag splits the 32-bit profile ID into
 * vendor (high 16) then profile number (low 16), each little-endian, ahead of
 * the tag number -- verified against CHIP's Encoding3, where profile
 * 0xAABBCCDD and tag 1 encode as BB AA DD CC 01 00.
 */
static bool put_tag(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t element_type)
{
	uint32_t profile = tag_profile(tag);
	uint32_t number = tag_number(tag);
	uint8_t control;
	size_t tag_octets;

	if (profile == MATTER_TLV_SPECIAL_PROFILE) {
		if (number == MATTER_TLV_ANON_TAG_NUM) {
			control = TC_ANON;
			tag_octets = 0u;
		} else if (number <= UINT8_MAX) {
			control = TC_CONTEXT;
			tag_octets = 1u;
		} else {
			return fail(w, MATTER_TLV_E_INVAL);
		}
	} else if (profile == MATTER_TLV_COMMON_PROFILE) {
		control = (number <= UINT16_MAX) ? TC_COMMON_2 : TC_COMMON_4;
		tag_octets = (number <= UINT16_MAX) ? 2u : 4u;
	} else if (w->implicit_set && profile == w->implicit_profile) {
		control = (number <= UINT16_MAX) ? TC_IMPLICIT_2 : TC_IMPLICIT_4;
		tag_octets = (number <= UINT16_MAX) ? 2u : 4u;
	} else {
		control = (number <= UINT16_MAX) ? TC_FULLQUAL_6 : TC_FULLQUAL_8;
		tag_octets = (number <= UINT16_MAX) ? 6u : 8u;
	}

	if (!room(w, 1u + tag_octets)) {
		return false;
	}

	w->buf[w->len++] = (uint8_t)(control | element_type);

	if (control == TC_FULLQUAL_6 || control == TC_FULLQUAL_8) {
		put_le(w, (profile >> 16) & 0xFFFFu, 2u); /* vendor ID */
		put_le(w, profile & 0xFFFFu, 2u);         /* profile number */
		put_le(w, number, tag_octets - 4u);
	} else if (tag_octets != 0u) {
		put_le(w, number, tag_octets);
	}

	return true;
}

/** True while the writer is healthy. Every public entry point starts here. */
static bool live(struct matter_tlv_writer *w)
{
	return w != NULL && w->rc == MATTER_TLV_OK;
}

void matter_tlv_writer_init(struct matter_tlv_writer *w, uint8_t *buf, size_t cap)
{
	if (w == NULL) {
		return;
	}
	memset(w, 0, sizeof(*w));
	w->buf = buf;
	w->cap = cap;
}

void matter_tlv_writer_set_implicit_profile(struct matter_tlv_writer *w, uint32_t profile)
{
	if (w == NULL) {
		return;
	}
	w->implicit_profile = profile;
	w->implicit_set = true;
}

int matter_tlv_put_bool(struct matter_tlv_writer *w, matter_tlv_tag_t tag, bool v)
{
	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}
	(void)put_tag(w, tag, v ? ET_BOOL_TRUE : ET_BOOL_FALSE);
	return w->rc;
}

int matter_tlv_put_null(struct matter_tlv_writer *w, matter_tlv_tag_t tag)
{
	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}
	(void)put_tag(w, tag, ET_NULL);
	return w->rc;
}

int matter_tlv_put_i64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, int64_t v)
{
	uint8_t type;
	size_t width;

	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}

	if (v >= INT8_MIN && v <= INT8_MAX) {
		type = ET_INT8;
		width = 1u;
	} else if (v >= INT16_MIN && v <= INT16_MAX) {
		type = ET_INT16;
		width = 2u;
	} else if (v >= INT32_MIN && v <= INT32_MAX) {
		type = ET_INT32;
		width = 4u;
	} else {
		type = ET_INT64;
		width = 8u;
	}

	if (!put_tag(w, tag, type) || !room(w, width)) {
		return w->rc;
	}
	put_le(w, (uint64_t)v, width);
	return w->rc;
}

int matter_tlv_put_u64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint64_t v)
{
	uint8_t type;
	size_t width;

	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}

	if (v <= UINT8_MAX) {
		type = ET_UINT8;
		width = 1u;
	} else if (v <= UINT16_MAX) {
		type = ET_UINT16;
		width = 2u;
	} else if (v <= UINT32_MAX) {
		type = ET_UINT32;
		width = 4u;
	} else {
		type = ET_UINT64;
		width = 8u;
	}

	if (!put_tag(w, tag, type) || !room(w, width)) {
		return w->rc;
	}
	put_le(w, v, width);
	return w->rc;
}

/** Shared body for the two string types; they differ only in the element-type base. */
static int put_string(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type_len1,
		      const void *data, size_t len)
{
	uint8_t type;
	size_t len_octets;

	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}
	if (data == NULL && len != 0u) {
		(void)fail(w, MATTER_TLV_E_INVAL);
		return w->rc;
	}

	if (len <= UINT8_MAX) {
		type = type_len1;
		len_octets = 1u;
	} else if (len <= UINT16_MAX) {
		type = (uint8_t)(type_len1 + 1u);
		len_octets = 2u;
	} else {
		/* 4-octet lengths are reachable in principle; an 8-octet one is not
		 * on a part with 128 KB of RAM, so it is rejected rather than
		 * carried as untestable code. */
		type = (uint8_t)(type_len1 + 2u);
		len_octets = 4u;
	}

	if (!put_tag(w, tag, type) || !room(w, len_octets + len)) {
		return w->rc;
	}
	put_le(w, (uint64_t)len, len_octets);
	if (len != 0u) {
		put_raw(w, data, len);
	}
	return w->rc;
}

int matter_tlv_put_utf8(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const char *s, size_t len)
{
	return put_string(w, tag, ET_UTF8_LEN1, s, len);
}

int matter_tlv_put_bytes(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const uint8_t *b,
			 size_t len)
{
	return put_string(w, tag, ET_BYTES_LEN1, b, len);
}

int matter_tlv_start_container(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type)
{
	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}
	if (type != MATTER_TLV_STRUCTURE && type != MATTER_TLV_ARRAY && type != MATTER_TLV_LIST) {
		(void)fail(w, MATTER_TLV_E_INVAL);
		return w->rc;
	}
	if (w->depth >= MATTER_TLV_MAX_DEPTH) {
		(void)fail(w, MATTER_TLV_E_DEPTH);
		return w->rc;
	}
	if (put_tag(w, tag, type)) {
		w->depth++;
	}
	return w->rc;
}

int matter_tlv_end_container(struct matter_tlv_writer *w)
{
	if (!live(w)) {
		return w == NULL ? MATTER_TLV_E_INVAL : w->rc;
	}
	if (w->depth == 0u) {
		(void)fail(w, MATTER_TLV_E_STATE);
		return w->rc;
	}
	if (!room(w, 1u)) {
		return w->rc;
	}
	/* End-of-container carries no tag, so it is a bare control byte. */
	w->buf[w->len++] = ET_END_CONTAINER;
	w->depth--;
	return w->rc;
}

int matter_tlv_writer_finish(struct matter_tlv_writer *w, size_t *out_len)
{
	if (w == NULL) {
		return MATTER_TLV_E_INVAL;
	}
	if (w->rc != MATTER_TLV_OK) {
		return w->rc;
	}
	if (w->depth != 0u) {
		return fail(w, MATTER_TLV_E_STATE), w->rc;
	}
	if (out_len != NULL) {
		*out_len = w->len;
	}
	return MATTER_TLV_OK;
}
