// DeviceResponse structural decoder for the Aliro step-up phase: a minimal, bounds-checked,
// depth-limited CBOR reader (definite-length core-deterministic only) plus the Table 8-22/7-1/7-2
// field walk. No crypto and no allocation; every parsed field is a slice of the caller's buffer.
// This is the wire-facing attack surface and is fuzzed on its own (tests/host/fuzz/fuzz_stepup.c).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Provenance: original. CBOR per RFC 8949 (definite lengths only, indefinite
 * rejected); the Aliro remapped-key layout from the v1.0 spec (§7.2 Table 7-1/
 * 7-2, §8.4.2 Table 8-22) and the §14.6 worked example. The code is original.
 */
#include <stdint.h>
#include <string.h>

#include "aliro_stepup.h"

/* ---- minimal CBOR cursor ------------------------------------------------- */

#define CB_MAX_DEPTH 20

struct cbor {
	const uint8_t *p;
	const uint8_t *end;
};

/* Read one CBOR head: major type + argument. Consumes the argument bytes (and,
 * for major type 7, the simple/float payload). Rejects indefinite lengths and
 * anything truncated. Returns 0 on success. */
static int cb_head(struct cbor *c, uint8_t *mt, uint64_t *arg)
{
	if (c->p >= c->end) {
		return -1;
	}
	uint8_t ib = *c->p++;
	uint8_t ai = ib & 0x1fu;

	*mt = (uint8_t)(ib >> 5);
	if (ai < 24u) {
		*arg = ai;
		return 0;
	}
	size_t need = (ai == 24u)   ? 1u
		      : (ai == 25u) ? 2u
		      : (ai == 26u) ? 4u
		      : (ai == 27u) ? 8u
				    : 0u;

	if (need == 0u || (size_t)(c->end - c->p) < need) {
		return -1; /* 28..31 (incl. indefinite) or truncated */
	}
	uint64_t v = 0;

	for (size_t i = 0; i < need; i++) {
		v = (v << 8) | c->p[i];
	}
	c->p += need;
	*arg = v;
	return 0;
}

static int cb_skip_d(struct cbor *c, int depth)
{
	uint8_t mt;
	uint64_t arg;

	if (depth > CB_MAX_DEPTH || cb_head(c, &mt, &arg) != 0) {
		return -1;
	}
	switch (mt) {
	case 0: /* uint */
	case 1: /* nint */
	case 7: /* simple/float (payload already consumed by cb_head) */
		return 0;
	case 2: /* bstr */
	case 3: /* tstr */
		if (arg > (uint64_t)(c->end - c->p)) {
			return -1;
		}
		c->p += (size_t)arg;
		return 0;
	case 4: /* array */
		for (uint64_t i = 0; i < arg; i++) {
			if (cb_skip_d(c, depth + 1) != 0) {
				return -1;
			}
		}
		return 0;
	case 5: /* map */
		for (uint64_t i = 0; i < arg; i++) {
			if (cb_skip_d(c, depth + 1) != 0 || cb_skip_d(c, depth + 1) != 0) {
				return -1;
			}
		}
		return 0;
	case 6: /* tag */
		return cb_skip_d(c, depth + 1);
	default:
		return -1;
	}
}

static int cb_skip(struct cbor *c)
{
	return cb_skip_d(c, 0);
}

static int cb_expect(struct cbor *c, uint8_t want_mt, uint64_t *arg)
{
	uint8_t mt;

	if (cb_head(c, &mt, arg) != 0 || mt != want_mt) {
		return -1;
	}
	return 0;
}

static int cb_map(struct cbor *c, uint64_t *n)
{
	return cb_expect(c, 5, n);
}

static int cb_arr(struct cbor *c, uint64_t *n)
{
	return cb_expect(c, 4, n);
}

static int cb_uint(struct cbor *c, uint64_t *v)
{
	return cb_expect(c, 0, v);
}

static int cb_bytes(struct cbor *c, uint8_t want_mt, const uint8_t **s, size_t *n)
{
	uint64_t arg;

	if (cb_expect(c, want_mt, &arg) != 0 || arg > (uint64_t)(c->end - c->p)) {
		return -1;
	}
	*s = c->p;
	*n = (size_t)arg;
	c->p += (size_t)arg;
	return 0;
}

static int cb_bstr(struct cbor *c, const uint8_t **s, size_t *n)
{
	return cb_bytes(c, 2, s, n);
}

static int cb_tstr(struct cbor *c, const uint8_t **s, size_t *n)
{
	return cb_bytes(c, 3, s, n);
}

/* Read a signed integer map key (uint or nint). */
static int cb_int_key(struct cbor *c, int64_t *v)
{
	uint8_t mt;
	uint64_t arg;

	if (cb_head(c, &mt, &arg) != 0) {
		return -1;
	}
	if (mt == 0) {
		*v = (int64_t)arg;
		return 0;
	}
	if (mt == 1) {
		*v = -1 - (int64_t)arg;
		return 0;
	}
	return -1;
}

static int cb_bool(struct cbor *c, int *b)
{
	uint8_t mt;
	uint64_t arg;

	if (cb_head(c, &mt, &arg) != 0 || mt != 7 || (arg != 20u && arg != 21u)) {
		return -1;
	}
	*b = (arg == 21u);
	return 0;
}

static int cb_tag(struct cbor *c, uint64_t *tag)
{
	return cb_expect(c, 6, tag);
}

/* Copy a text string into a fixed char buffer (NUL-terminated, truncated). */
static void str_copy(char *dst, size_t cap, const uint8_t *s, size_t n)
{
	size_t k = (n < cap - 1) ? n : cap - 1;

	memcpy(dst, s, k);
	dst[k] = '\0';
}

/* Match a 1-byte text key "1".."9" without allocating. */
static int key_is(const uint8_t *s, size_t n, char c)
{
	return n == 1u && s[0] == (uint8_t)c;
}

/* ---- RFC 3339 tdate (UTC "Z" form) -> epoch seconds ---------------------- */

static int digit2(const uint8_t *s, int *out)
{
	if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') {
		return -1;
	}
	*out = (s[0] - '0') * 10 + (s[1] - '0');
	return 0;
}

/* days since 1970-01-01 for a proleptic-Gregorian civil date (Hinnant). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
	y -= (m <= 2);
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
	unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;

	return era * 146097 + (int64_t)doe - 719468;
}

/* Parse "YYYY-MM-DDTHH:MM:SSZ" (20 chars). Returns 0 and *epoch, else -1. */
static int tdate_epoch(const uint8_t *s, size_t n, int64_t *epoch)
{
	if (n != 20u || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') ||
	    s[13] != ':' || s[16] != ':' || s[19] != 'Z') {
		return -1;
	}
	int yhi, ylo, mo, da, hh, mm, ss;

	if (digit2(s, &yhi) || digit2(s + 2, &ylo) || digit2(s + 5, &mo) || digit2(s + 8, &da) ||
	    digit2(s + 11, &hh) || digit2(s + 14, &mm) || digit2(s + 17, &ss)) {
		return -1;
	}
	int year = yhi * 100 + ylo;

	if (mo < 1 || mo > 12 || da < 1 || da > 31 || hh > 23 || mm > 59 || ss > 60) {
		return -1;
	}
	int64_t days = days_from_civil(year, (unsigned)mo, (unsigned)da);

	*epoch = days * 86400 + hh * 3600 + mm * 60 + ss;
	return 0;
}

/* ---- MobileSecurityObject (Table 7-1) ------------------------------------ */

static int parse_validity(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t n;

	if (cb_map(c, &n) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(c, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '5')) { /* validityIteration */
			if (cb_uint(c, &doc->iteration) != 0) {
				return -1;
			}
			doc->have_iteration = 1;
			continue;
		}
		if (key_is(k, kl, '1') || key_is(k, kl, '2') || key_is(k, kl, '3')) {
			uint64_t tag;
			const uint8_t *s;
			size_t sl;
			int64_t ep;

			if (cb_tag(c, &tag) != 0 || tag != 0u || cb_tstr(c, &s, &sl) != 0) {
				return -1;
			}
			int ok = tdate_epoch(s, sl, &ep) == 0;

			if (k[0] == '1') {
				doc->have_signed = ok;
				doc->signed_epoch = ep;
			} else if (k[0] == '2') {
				doc->have_valid_from = ok;
				doc->valid_from_epoch = ep;
			} else {
				doc->have_valid_until = ok;
				doc->valid_until_epoch = ep;
			}
			continue;
		}
		if (cb_skip(c) != 0) { /* expectedUpdate "4" or unknown */
			return -1;
		}
	}
	return 0;
}

static int parse_value_digests(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t nns;

	if (cb_map(c, &nns) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < nns; i++) {
		const uint8_t *ns;
		size_t nsl;
		uint64_t nd;

		if (cb_tstr(c, &ns, &nsl) != 0 || cb_map(c, &nd) != 0) {
			return -1;
		}
		for (uint64_t j = 0; j < nd; j++) {
			uint64_t id;
			const uint8_t *h;
			size_t hl;

			if (cb_uint(c, &id) != 0 || cb_bstr(c, &h, &hl) != 0) {
				return -1;
			}
			if (hl == 32u && doc->n_digests < ALIRO_STEPUP_MAX_DIGESTS) {
				doc->digests[doc->n_digests].id = id;
				memcpy(doc->digests[doc->n_digests].hash, h, 32);
				doc->n_digests++;
			}
		}
	}
	return 0;
}

static int parse_mso(const uint8_t *mso, size_t mso_len, struct aliro_stepup_doc *doc)
{
	struct cbor c = {mso, mso + mso_len};
	uint64_t n;

	if (cb_map(&c, &n) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(&c, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '2')) {
			const uint8_t *s;
			size_t sl;

			if (cb_tstr(&c, &s, &sl) != 0) {
				return -1;
			}
			str_copy(doc->digest_alg, sizeof(doc->digest_alg), s, sl);
		} else if (key_is(k, kl, '3')) {
			if (parse_value_digests(&c, doc) != 0) {
				return -1;
			}
		} else if (key_is(k, kl, '5')) {
			const uint8_t *s;
			size_t sl;

			if (cb_tstr(&c, &s, &sl) != 0) {
				return -1;
			}
			str_copy(doc->mso_doc_type, sizeof(doc->mso_doc_type), s, sl);
		} else if (key_is(k, kl, '6')) {
			if (parse_validity(&c, doc) != 0) {
				return -1;
			}
		} else if (key_is(k, kl, '7')) {
			if (cb_bool(&c, &doc->time_verification_required) != 0) {
				return -1;
			}
		} else if (cb_skip(&c) != 0) { /* "1" version, "4" deviceKeyInfo, unknown */
			return -1;
		}
	}
	return 0;
}

/* ---- IssuerAuth COSE_Sign1 ----------------------------------------------- */

static int parse_issuer_auth(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t n;

	if (cb_arr(c, &n) != 0 || n != 4u) {
		return -1;
	}
	if (cb_bstr(c, &doc->protected_hdr, &doc->protected_len) != 0) {
		return -1;
	}

	uint64_t nu;

	if (cb_map(c, &nu) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < nu; i++) {
		int64_t label;

		if (cb_int_key(c, &label) != 0) {
			return -1;
		}
		if (label == 4) { /* kid */
			if (cb_bstr(c, &doc->kid, &doc->kid_len) != 0) {
				return -1;
			}
		} else if (label == 33) { /* x5chain: record the raw value item bytes */
			const uint8_t *start = c->p;

			if (cb_skip(c) != 0) {
				return -1;
			}
			doc->x5chain = start;
			doc->x5chain_len = (size_t)(c->p - start);
		} else if (cb_skip(c) != 0) {
			return -1;
		}
	}

	const uint8_t *payload;
	size_t payload_len;
	const uint8_t *sig;
	size_t sig_len;

	if (cb_bstr(c, &payload, &payload_len) != 0 || cb_bstr(c, &sig, &sig_len) != 0 ||
	    sig_len != 64u) {
		return -1;
	}
	doc->signature = sig;

	/* payload = 24(bstr(MSO)); unwrap the tag + inner bstr, keep both the raw
	 * payload slice (for the Sig_structure) and the decoded MSO. */
	doc->payload = payload;
	doc->payload_len = payload_len;

	struct cbor pc = {payload, payload + payload_len};
	uint64_t tag;
	const uint8_t *mso;
	size_t mso_len;

	if (cb_tag(&pc, &tag) != 0 || tag != 24u || cb_bstr(&pc, &mso, &mso_len) != 0) {
		return -1;
	}
	return parse_mso(mso, mso_len, doc);
}

/* ---- IssuerSignedItems --------------------------------------------------- */

static int parse_one_item(struct cbor *c, struct aliro_stepup_item *it)
{
	const uint8_t *start = c->p;
	uint64_t tag;
	const uint8_t *inner;
	size_t inner_len;

	if (cb_tag(c, &tag) != 0 || tag != 24u || cb_bstr(c, &inner, &inner_len) != 0) {
		return -1;
	}
	it->tagged = start;
	it->tagged_len = (size_t)(c->p - start);

	struct cbor ic = {inner, inner + inner_len};
	uint64_t n;

	if (cb_map(&ic, &n) != 0) {
		return -1;
	}
	it->elem_id[0] = '\0';
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(&ic, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '1')) {
			if (cb_uint(&ic, &it->digest_id) != 0) {
				return -1;
			}
		} else if (key_is(k, kl, '3')) {
			const uint8_t *s;
			size_t sl;

			if (cb_tstr(&ic, &s, &sl) != 0) {
				return -1;
			}
			str_copy(it->elem_id, sizeof(it->elem_id), s, sl);
		} else if (cb_skip(&ic) != 0) { /* "2" random, "4" elementValue */
			return -1;
		}
	}
	return 0;
}

static int parse_name_spaces(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t nns;

	if (cb_map(c, &nns) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < nns; i++) {
		const uint8_t *ns;
		size_t nsl;
		uint64_t nit;

		if (cb_tstr(c, &ns, &nsl) != 0 || cb_arr(c, &nit) != 0) {
			return -1;
		}
		if (doc->name_space[0] == '\0') {
			str_copy(doc->name_space, sizeof(doc->name_space), ns, nsl);
		}
		for (uint64_t j = 0; j < nit; j++) {
			if (doc->n_items < ALIRO_STEPUP_MAX_ITEMS) {
				if (parse_one_item(c, &doc->items[doc->n_items]) != 0) {
					return -1;
				}
				doc->n_items++;
			} else if (cb_skip(c) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int parse_issuer_signed(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t n;

	if (cb_map(c, &n) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(c, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '1')) {
			if (parse_name_spaces(c, doc) != 0) {
				return -1;
			}
		} else if (key_is(k, kl, '2')) {
			if (parse_issuer_auth(c, doc) != 0) {
				return -1;
			}
		} else if (cb_skip(c) != 0) {
			return -1;
		}
	}
	return 0;
}

static int parse_document(struct cbor *c, struct aliro_stepup_doc *doc)
{
	uint64_t n;

	if (cb_map(c, &n) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(c, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '1')) {
			if (parse_issuer_signed(c, doc) != 0) {
				return -1;
			}
		} else if (key_is(k, kl, '5')) {
			const uint8_t *s;
			size_t sl;

			if (cb_tstr(c, &s, &sl) != 0) {
				return -1;
			}
			str_copy(doc->doc_type, sizeof(doc->doc_type), s, sl);
		} else if (cb_skip(c) != 0) { /* "3" deviceSigned must be absent (§8.4.2), skip */
			return -1;
		}
	}
	return 0;
}

int aliro_stepup_parse_response(const uint8_t *buf, size_t len, struct aliro_stepup_doc *doc)
{
	if (buf == NULL || doc == NULL) {
		return -1;
	}
	memset(doc, 0, sizeof(*doc));

	struct cbor c = {buf, buf + len};
	uint64_t n;

	if (cb_map(&c, &n) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < n; i++) {
		const uint8_t *k;
		size_t kl;

		if (cb_tstr(&c, &k, &kl) != 0) {
			return -1;
		}
		if (key_is(k, kl, '2')) { /* documents */
			uint64_t nd;

			if (cb_arr(&c, &nd) != 0) {
				return -1;
			}
			for (uint64_t j = 0; j < nd; j++) {
				if (j == 0) {
					doc->have_document = 1;
					if (parse_document(&c, doc) != 0) {
						return -1;
					}
				} else if (cb_skip(&c) != 0) { /* only the first document is used */
					return -1;
				}
			}
		} else if (key_is(k, kl, '3')) { /* status */
			uint64_t st;

			if (cb_uint(&c, &st) != 0) {
				return -1;
			}
			doc->status = (int)st;
		} else if (cb_skip(&c) != 0) { /* "1" version, unknown */
			return -1;
		}
	}
	return 0;
}
