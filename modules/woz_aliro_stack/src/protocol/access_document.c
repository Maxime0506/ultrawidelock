#include "access_document.h"

#include <string.h>

struct item {
	uint8_t major;
	uint64_t value;
	const uint8_t *encoded;
	size_t encoded_length;
	const uint8_t *payload;
	size_t payload_length;
};

static int head(const uint8_t *data, size_t length, size_t *offset, uint8_t *major, uint64_t *value)
{
	if (*offset >= length) {
		return -1;
	}
	uint8_t b = data[(*offset)++];
	*major = b >> 5;
	uint8_t ai = b & 31;
	if (ai < 24) {
		*value = ai;
		return 0;
	}
	size_t n = ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : ai == 27 ? 8 : 0;
	if (n == 0 || n > length - *offset) {
		return -1;
	}
	uint64_t v = 0;
	for (size_t i = 0; i < n; ++i) {
		v = (v << 8) | data[(*offset)++];
	}
	if ((n == 1 && v < 24) || (n == 2 && v <= 0xff) || (n == 4 && v <= 0xffff) ||
	    (n == 8 && v <= 0xffffffffu)) {
		return -1;
	}
	*value = v;
	return 0;
}

static int child_count(uint8_t major, uint64_t value, uint64_t *count)
{
	if (major == 4) {
		*count = value;
		return 0;
	}
	if (major == 5) {
		if (value > UINT64_MAX / 2) {
			return -1;
		}
		*count = value * 2;
		return 0;
	}
	if (major == 6) {
		*count = 1;
		return 0;
	}
	return -1;
}

static int parse_at(const uint8_t *data, size_t length, size_t *offset, struct item *out)
{
	/* Access Documents are handled on a small embedded workqueue stack. Walk
	 * nested CBOR iteratively so an otherwise valid document cannot exhaust
	 * that stack. This preserves the former maximum nesting depth of 24. */
	enum {
		MAX_CONTAINER_DEPTH = 25
	};
	uint64_t remaining[MAX_CONTAINER_DEPTH];
	size_t depth = 0;

	if (data == NULL || offset == NULL || out == NULL || *offset > length) {
		return -1;
	}
	const size_t start = *offset;
	uint8_t major;
	uint64_t value;
	if (head(data, length, offset, &major, &value) != 0) {
		return -1;
	}
	out->major = major;
	out->value = value;
	out->encoded = data + start;
	out->payload = NULL;
	out->payload_length = 0;

	if (major == 2 || major == 3) {
		if (value > length - *offset) {
			return -1;
		}
		out->payload = data + *offset;
		out->payload_length = (size_t)value;
		*offset += (size_t)value;
	} else if (major == 4 || major == 5 || major == 6) {
		uint64_t count;
		if (child_count(major, value, &count) != 0 || count > length - *offset) {
			return -1;
		}
		remaining[depth++] = count;
	} else if (major == 7) {
		if (value > 23) {
			return -1; /* no floats/simple-value payloads */
		}
	} else if (major != 0 && major != 1) {
		return -1;
	}

	while (depth != 0) {
		if (remaining[depth - 1] == 0) {
			--depth;
			continue;
		}
		/* A child here would be deeper than the former depth-24 limit. */
		if (depth >= MAX_CONTAINER_DEPTH) {
			return -1;
		}
		--remaining[depth - 1];

		uint8_t childMajor;
		uint64_t childValue;
		if (head(data, length, offset, &childMajor, &childValue) != 0) {
			return -1;
		}
		if (childMajor == 2 || childMajor == 3) {
			if (childValue > length - *offset) {
				return -1;
			}
			*offset += (size_t)childValue;
		} else if (childMajor == 4 || childMajor == 5 || childMajor == 6) {
			uint64_t count;
			if (depth >= MAX_CONTAINER_DEPTH ||
			    child_count(childMajor, childValue, &count) != 0 ||
			    count > length - *offset) {
				return -1;
			}
			remaining[depth++] = count;
		} else if (childMajor == 7) {
			if (childValue > 23) {
				return -1;
			}
		} else if (childMajor != 0 && childMajor != 1) {
			return -1;
		}
	}
	out->encoded_length = *offset - start;
	return 0;
}

static int root(const uint8_t *data, size_t length, struct item *out)
{
	size_t off = 0;
	return parse_at(data, length, &off, out) == 0 && off == length ? 0 : -1;
}

static int child_at(const struct item *container, size_t wanted, struct item *out)
{
	if (container->major != 4 && container->major != 5) {
		return -1;
	}
	/* Container payload isn't recorded by parse_at, derive it by re-reading its head. */
	uint8_t major;
	uint64_t count;
	size_t off = 0;
	if (head(container->encoded, container->encoded_length, &off, &major, &count) != 0) {
		return -1;
	}
	for (size_t i = 0; i <= wanted; ++i) {
		if (parse_at(container->encoded, container->encoded_length, &off, out) != 0) {
			return -1;
		}
	}
	return 0;
}

static int map_find_text(const struct item *map, const char *key, struct item *value)
{
	if (map->major != 5) {
		return -1;
	}
	size_t off = 0;
	uint8_t major;
	uint64_t count;
	if (head(map->encoded, map->encoded_length, &off, &major, &count) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < count; ++i) {
		struct item k, v;
		if (parse_at(map->encoded, map->encoded_length, &off, &k) != 0 ||
		    parse_at(map->encoded, map->encoded_length, &off, &v) != 0) {
			return -1;
		}
		if (k.major == 3 && k.payload_length == strlen(key) &&
		    memcmp(k.payload, key, k.payload_length) == 0) {
			*value = v;
			return 0;
		}
	}
	return -1;
}

static int integer(const struct item *item, int64_t *value)
{
	if (item->major == 0 && item->value <= INT64_MAX) {
		*value = (int64_t)item->value;
		return 0;
	}
	if (item->major == 1 && item->value <= INT64_MAX) {
		*value = -(int64_t)item->value - 1;
		return 0;
	}
	return -1;
}

static int map_find_int(const struct item *map, int64_t key, struct item *value)
{
	if (map->major != 5) {
		return -1;
	}
	size_t off = 0;
	uint8_t major;
	uint64_t count;
	if (head(map->encoded, map->encoded_length, &off, &major, &count) != 0) {
		return -1;
	}
	for (uint64_t i = 0; i < count; ++i) {
		struct item k, v;
		int64_t actual;
		if (parse_at(map->encoded, map->encoded_length, &off, &k) != 0 ||
		    parse_at(map->encoded, map->encoded_length, &off, &v) != 0) {
			return -1;
		}
		if (integer(&k, &actual) == 0 && actual == key) {
			*value = v;
			return 0;
		}
	}
	return -1;
}

static int tagged_embedded(const struct item *tag, struct item *embedded)
{
	if (tag->major != 6 || tag->value != 24) {
		return -1;
	}
	size_t off = 0;
	uint8_t major;
	uint64_t value;
	if (head(tag->encoded, tag->encoded_length, &off, &major, &value) != 0 ||
	    parse_at(tag->encoded, tag->encoded_length, &off, embedded) != 0 ||
	    embedded->major != 2 || off != tag->encoded_length) {
		return -1;
	}
	return 0;
}

static int timestamp(const struct item *item, uint8_t output[20])
{
	struct item text = *item;
	if (item->major == 6) {
		size_t off = 0;
		uint8_t major;
		uint64_t tag;
		if (head(item->encoded, item->encoded_length, &off, &major, &tag) != 0 ||
		    tag != 0 || parse_at(item->encoded, item->encoded_length, &off, &text) != 0) {
			return -1;
		}
	}
	if (text.major != 3 || text.payload_length != 20) {
		return -1;
	}
	memcpy(output, text.payload, 20);
	return 0;
}

int woz_aliro_parse_access_document(const uint8_t *data, size_t length, const uint8_t *requested,
				    size_t requested_length, struct woz_aliro_access_document *r)
{
	if (data == NULL || requested == NULL || requested_length == 0 || r == NULL) {
		return -1;
	}
	memset(r, 0, sizeof(*r));
	struct item top, version, documents, status, document, issuer, namespaces, nsitems,
		tagged_item, item_map;
	if (root(data, length, &top) != 0 || top.major != 5 || top.value != 3) {
		return -10;
	}
	if (map_find_text(&top, "1", &version) != 0 || version.major != 3 ||
	    version.payload_length != 3 || memcmp(version.payload, "1.0", 3) != 0) {
		return -11;
	}
	if (map_find_text(&top, "2", &documents) != 0 || documents.major != 4 ||
	    documents.value != 1 || map_find_text(&top, "3", &status) != 0 || status.major != 0 ||
	    status.value != 0) {
		return -12;
	}
	if (child_at(&documents, 0, &document) != 0 || document.major != 5 || document.value != 2 ||
	    map_find_text(&document, "5", &version) != 0 || version.major != 3 ||
	    version.payload_length != 7 || memcmp(version.payload, "aliro-a", 7) != 0) {
		return -13;
	}
	if (map_find_text(&document, "1", &issuer) != 0 || issuer.major != 5 || issuer.value != 2 ||
	    map_find_text(&issuer, "1", &namespaces) != 0 ||
	    map_find_text(&namespaces, "aliro-a", &nsitems) != 0 || nsitems.major != 4 ||
	    nsitems.value == 0) {
		return -14;
	}

	struct item digest_id, identifier, element;
	bool found_item = false;
	for (size_t i = 0; i < nsitems.value; ++i) {
		struct item embedded;
		if (child_at(&nsitems, i, &tagged_item) != 0 ||
		    tagged_embedded(&tagged_item, &embedded) != 0 ||
		    root(embedded.payload, embedded.payload_length, &item_map) != 0) {
			return -20;
		}
		if (map_find_text(&item_map, "3", &identifier) == 0 && identifier.major == 3 &&
		    identifier.payload_length == requested_length &&
		    memcmp(identifier.payload, requested, requested_length) == 0) {
			found_item = true;
			break;
		}
	}
	if (!found_item || map_find_text(&item_map, "1", &digest_id) != 0 || digest_id.major != 0 ||
	    map_find_text(&item_map, "4", &element) != 0) {
		return -21;
	}
	r->digest_id = digest_id.value;
	r->data_element = element.encoded;
	r->data_element_length = element.encoded_length;
	r->issuer_signed_item = tagged_item.encoded;
	r->issuer_signed_item_length = tagged_item.encoded_length;

	struct item auth, protected_bstr, unprotected, payload_bstr, signature;
	if (map_find_text(&issuer, "2", &auth) != 0 || auth.major != 4 || auth.value != 4 ||
	    child_at(&auth, 0, &protected_bstr) != 0 || protected_bstr.major != 2 ||
	    child_at(&auth, 1, &unprotected) != 0 || child_at(&auth, 2, &payload_bstr) != 0 ||
	    payload_bstr.major != 2 || child_at(&auth, 3, &signature) != 0 ||
	    signature.major != 2 || signature.payload_length != 64) {
		return -30;
	}
	struct item kid;
	if (map_find_int(&unprotected, 4, &kid) == 0) {
		if (kid.major != 2 || kid.payload_length == 0 || kid.payload_length > 8) {
			return -31;
		}
		r->issuer_kid = kid.payload;
		r->issuer_kid_length = kid.payload_length;
	}
	struct item x5chain;
	if (map_find_int(&unprotected, 33, &x5chain) == 0) {
		struct item certificate = x5chain;
		if (x5chain.major == 4) {
			if (x5chain.value == 0 || child_at(&x5chain, 0, &certificate) != 0) {
				return -32;
			}
		}
		if (certificate.major != 2 || certificate.payload_length == 0) {
			return -32;
		}
		r->issuer_certificate = certificate.payload;
		r->issuer_certificate_length = certificate.payload_length;
	}
	if (r->issuer_kid_length == 0 && r->issuer_certificate_length == 0) {
		return -33;
	}
	struct item protected_map, alg;
	int64_t algorithm_id;
	if (root(protected_bstr.payload, protected_bstr.payload_length, &protected_map) != 0 ||
	    map_find_int(&protected_map, 1, &alg) != 0 || integer(&alg, &algorithm_id) != 0 ||
	    algorithm_id != -7) {
		return -34;
	}
	r->cose_protected = protected_bstr.payload;
	r->cose_protected_length = protected_bstr.payload_length;
	r->cose_payload = payload_bstr.payload;
	r->cose_payload_length = payload_bstr.payload_length;
	r->cose_signature = signature.payload;

	struct item tagged_mso, mso_bstr, mso, algorithm, digests, doc_digests, digest, key_info,
		cose_key, x, y, validity;
	if (root(payload_bstr.payload, payload_bstr.payload_length, &tagged_mso) != 0 ||
	    tagged_embedded(&tagged_mso, &mso_bstr) != 0 ||
	    root(mso_bstr.payload, mso_bstr.payload_length, &mso) != 0) {
		return -40;
	}
	if (map_find_text(&mso, "2", &algorithm) != 0 || algorithm.major != 3 ||
	    algorithm.payload_length != 7 || memcmp(algorithm.payload, "SHA-256", 7) != 0 ||
	    map_find_text(&mso, "5", &version) != 0 || version.major != 3 ||
	    version.payload_length != 7 || memcmp(version.payload, "aliro-a", 7) != 0 ||
	    map_find_text(&mso, "3", &digests) != 0 ||
	    map_find_text(&digests, "aliro-a", &doc_digests) != 0 ||
	    map_find_int(&doc_digests, (int64_t)r->digest_id, &digest) != 0 || digest.major != 2 ||
	    digest.payload_length != 32) {
		return -41;
	}
	if (map_find_text(&mso, "4", &key_info) != 0 ||
	    (map_find_text(&key_info, "1", &cose_key) != 0 &&
	     map_find_text(&key_info, "4", &cose_key) != 0) ||
	    map_find_int(&cose_key, -2, &x) != 0 || x.major != 2 || x.payload_length != 32 ||
	    map_find_int(&cose_key, -3, &y) != 0 || y.major != 2 || y.payload_length != 32) {
		return -42;
	}
	if (map_find_text(&mso, "6", &validity) != 0) {
		return -43;
	}
	r->expected_digest = digest.payload;
	struct item key_type, curve;
	int64_t key_type_value, curve_value;
	if (map_find_int(&cose_key, 1, &key_type) != 0 ||
	    integer(&key_type, &key_type_value) != 0 || key_type_value != 2 ||
	    map_find_int(&cose_key, -1, &curve) != 0 || integer(&curve, &curve_value) != 0 ||
	    curve_value != 1) {
		return -44;
	}
	r->device_public_key[0] = 4;
	memcpy(r->device_public_key + 1, x.payload, 32);
	memcpy(r->device_public_key + 33, y.payload, 32);
	struct item signed_time, valid_from, valid_until, required, iteration;
	if (map_find_text(&validity, "1", &signed_time) != 0 ||
	    timestamp(&signed_time, r->signed_timestamp) != 0 ||
	    map_find_text(&validity, "2", &valid_from) != 0 ||
	    timestamp(&valid_from, r->valid_from) != 0 ||
	    map_find_text(&validity, "3", &valid_until) != 0 ||
	    timestamp(&valid_until, r->valid_until) != 0 ||
	    map_find_text(&mso, "7", &required) != 0 || required.major != 7 ||
	    (required.value != 20 && required.value != 21)) {
		return -45;
	}
	r->time_verification_required = required.value == 21;
	if (map_find_text(&validity, "5", &iteration) == 0) {
		if (iteration.major != 0) {
			return -46;
		}
		r->has_validity_iteration = true;
		r->validity_iteration = iteration.value;
	}
	return 0;
}
