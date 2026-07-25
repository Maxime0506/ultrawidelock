// Aliro step-up phase codec + verifier: derives the StepUpSK SessionData keys, builds the mdoc
// DeviceRequest and its ENVELOPE/GET RESPONSE APDUs, seals/opens SessionData over the aliro_secchan
// AES-256-GCM channel, and runs the six-step Access Document verification of spec 7.4. The ES256
// primitive is injected (verify ctx) so this unit carries no elliptic-curve dependency.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Provenance: original. Wire structures from the Aliro v1.0 spec (§7.4, §8.4,
 * §14.6) and ISO 18013-5 (SessionData, COSE_Sign1 Sig_structure). The code is
 * original; the crypto goes through aliro_hash (HKDF/SHA-256) and aliro_crypto
 * (AES-256-GCM secure channel), with ES256 supplied by the caller.
 */
#include <stdint.h>
#include <string.h>

#include "aliro_crypto.h"
#include "aliro_hash.h"
#include "aliro_stepup.h"

/* ---- StepUpSK session keys (§8.4.3) -------------------------------------- */

/**
 * Derive Step-Up reader and device session keys (SKReader and SKDevice) from the Step-Up segment of
 * a key block using HKDF with null salt; return 0 on success or -1 if HKDF fails.
 */
int aliro_stepup_derive_keys(const uint8_t block[ALIRO_KEY_BLOCK_LEN],
			     uint8_t sk_reader[ALIRO_SESSION_KEY_LEN],
			     uint8_t sk_device[ALIRO_SESSION_KEY_LEN])
{
	const uint8_t *stepup = block + ALIRO_STEPUP_SK_OFFSET;

	if (aliro_hkdf(NULL, 0, stepup, ALIRO_SESSION_KEY_LEN, (const uint8_t *)"SKReader", 8,
		       sk_reader, ALIRO_SESSION_KEY_LEN) != 0 ||
	    aliro_hkdf(NULL, 0, stepup, ALIRO_SESSION_KEY_LEN, (const uint8_t *)"SKDevice", 8,
		       sk_device, ALIRO_SESSION_KEY_LEN) != 0) {
		return -1;
	}
	return 0;
}

/**
 * Initialize a session-key security channel for Step-Up step-up protocol using reader and device
 * session keys derived from key material.
 */
void aliro_stepup_channel_init(struct aliro_secchan *sc,
			       const uint8_t sk_reader[ALIRO_SESSION_KEY_LEN],
			       const uint8_t sk_device[ALIRO_SESSION_KEY_LEN])
{
	aliro_secchan_init(sc, sk_reader, sk_device);
}

/* ---- tiny CBOR writer (definite lengths) --------------------------------- */

/**
 * CBOR writer: tracks output buffer pointer, end, and error state for incremental encoding of
 * BER-TLV and CBOR primitives.
 */
struct cw {
	uint8_t *p;
	uint8_t *end;
	int err;
};

/**
 * Append n bytes from buffer b to the CBOR writer; set error flag and return early if no space
 * remains or writer is already in error state.
 */
static void cw_raw(struct cw *w, const void *b, size_t n)
{
	if (w->err || (size_t)(w->end - w->p) < n) {
		w->err = 1;
		return;
	}
	memcpy(w->p, b, n);
	w->p += n;
}

/**
 * Encode a CBOR major type and argument into the writer as 1, 2, 3, or 5 bytes depending on
 * argument magnitude.
 */
static void cw_type(struct cw *w, uint8_t major, uint64_t arg)
{
	uint8_t h[9];
	size_t n;

	if (arg < 24u) {
		h[0] = (uint8_t)(major | arg);
		n = 1;
	} else if (arg < 256u) {
		h[0] = (uint8_t)(major | 24u);
		h[1] = (uint8_t)arg;
		n = 2;
	} else if (arg < 65536u) {
		h[0] = (uint8_t)(major | 25u);
		h[1] = (uint8_t)(arg >> 8);
		h[2] = (uint8_t)arg;
		n = 3;
	} else {
		h[0] = (uint8_t)(major | 26u);
		h[1] = (uint8_t)(arg >> 24);
		h[2] = (uint8_t)(arg >> 16);
		h[3] = (uint8_t)(arg >> 8);
		h[4] = (uint8_t)arg;
		n = 5;
	}
	cw_raw(w, h, n);
}

/**
 * Encode a CBOR map header with n key-value pairs.
 */
static void cw_map(struct cw *w, uint64_t n)
{
	cw_type(w, 0xa0u, n);
}
/**
 * Encode a CBOR array header with n elements.
 */
static void cw_arr(struct cw *w, uint64_t n)
{
	cw_type(w, 0x80u, n);
}
/**
 * Encode a CBOR text string header and payload.
 */
static void cw_tstr(struct cw *w, const char *s)
{
	size_t n = strlen(s);

	cw_type(w, 0x60u, n);
	cw_raw(w, s, n);
}
/**
 * Encode a CBOR byte string header and payload.
 */
static void cw_bstr(struct cw *w, const uint8_t *b, size_t n)
{
	cw_type(w, 0x40u, n);
	cw_raw(w, b, n);
}
/**
 * Encode a CBOR semantic tag number.
 */
static void cw_tag(struct cw *w, uint64_t t)
{
	cw_type(w, 0xc0u, t);
}
/**
 * Append a CBOR boolean (0xf5 for true, 0xf4 for false) to the writer.
 */
static void cw_bool(struct cw *w, int v)
{
	uint8_t b = v ? 0xf5u : 0xf4u;

	cw_raw(w, &b, 1);
}

/* ---- DeviceRequest (Table 8-21) ------------------------------------------ */

static const char *const k_default_elems[] = {"element2", "element4"};

/**
 * Build a CBOR-encoded Step-Up deviceRequest with itemsRequest containing requested element names
 * (or default AccessCode, AccessLevel if none given) and docType "aliro-a"; return 0 on success or
 * -1 on buffer overflow.
 */
int aliro_stepup_build_device_request(const char *const *elems, size_t n_elems, uint8_t *out,
				      size_t cap, size_t *out_len)
{
	if (elems == NULL || n_elems == 0) {
		elems = k_default_elems;
		n_elems = 2;
	}

	/* itemsRequest = { "1": { "aliro-a": { <elem>: true, ... } }, "5": "aliro-a" } */
	uint8_t items[128];
	struct cw iw = {items, items + sizeof(items), 0};

	cw_map(&iw, 2);
	cw_tstr(&iw, "1"); /* nameSpaces */
	cw_map(&iw, 1);
	cw_tstr(&iw, ALIRO_STEPUP_DOCTYPE_ACCESS); /* namespace "aliro-a" */
	cw_map(&iw, n_elems);
	for (size_t i = 0; i < n_elems; i++) {
		cw_tstr(&iw, elems[i]);
		cw_bool(&iw, 1); /* intent to retain */
	}
	cw_tstr(&iw, "5"); /* docType */
	cw_tstr(&iw, ALIRO_STEPUP_DOCTYPE_ACCESS);
	if (iw.err) {
		return -1;
	}
	size_t items_len = (size_t)(iw.p - items);

	/* DeviceRequest = { "1": "1.0", "2": [ { "1": 24(bstr(itemsRequest)) } ] } */
	struct cw w = {out, out + cap, 0};

	cw_map(&w, 2);
	cw_tstr(&w, "1");
	cw_tstr(&w, "1.0");
	cw_tstr(&w, "2");
	cw_arr(&w, 1);
	cw_map(&w, 1);
	cw_tstr(&w, "1");
	cw_tag(&w, 24);
	cw_bstr(&w, items, items_len);
	if (w.err) {
		return -1;
	}
	*out_len = (size_t)(w.p - out);
	return 0;
}

/* ---- SessionData {"data": bstr} over the StepUpSK channel (§8.4.3) -------- */

/**
 * Seal a plaintext Step-Up response into a CBOR map with key "data" containing BER-TLV CBOR byte
 * string; return 0 on success or -1 on encryption or buffer errors.
 */
int aliro_stepup_seal_sessiondata(struct aliro_secchan *sc, const uint8_t *plain, size_t plain_len,
				  uint8_t *out, size_t cap, size_t *out_len)
{
	uint8_t blob[512];

	if (plain_len + ALIRO_GCM_TAG_LEN > sizeof(blob)) {
		return -1;
	}
	if (aliro_secchan_seal(sc, NULL, 0, plain, plain_len, blob, blob + plain_len) != 0) {
		return -1;
	}
	struct cw w = {out, out + cap, 0};

	cw_map(&w, 1);
	cw_tstr(&w, "data");
	cw_bstr(&w, blob, plain_len + ALIRO_GCM_TAG_LEN);
	if (w.err) {
		return -1;
	}
	*out_len = (size_t)(w.p - out);
	return 0;
}

/**
 * Decrypt a CBOR-wrapped sessionData blob (tag 0xa1, key "data", BER-TLV CBOR bstr) using the
 * security channel; return 0 and write plaintext length to *out_len on success, or -1 if format is
 * invalid, authentication fails, or output capacity is exceeded.
 */
int aliro_stepup_open_sessiondata(struct aliro_secchan *sc, const uint8_t *sd, size_t sd_len,
				  uint8_t *out, size_t cap, size_t *out_len)
{
	/* { "data": bstr }: a1 64 "data" <bstr-hdr> <blob> */
	if (sd_len < 8u || sd[0] != 0xa1u || sd[1] != 0x64u || memcmp(sd + 2, "data", 4) != 0) {
		return -1;
	}
	size_t i = 6;
	uint8_t ib = sd[i++];
	uint64_t blob_len;

	if (ib >= 0x40u && ib <= 0x57u) {
		blob_len = ib - 0x40u;
	} else if (ib == 0x58u) {
		if (i >= sd_len) {
			return -1;
		}
		blob_len = sd[i++];
	} else if (ib == 0x59u) {
		if (i + 2 > sd_len) {
			return -1;
		}
		blob_len = ((uint64_t)sd[i] << 8) | sd[i + 1];
		i += 2;
	} else {
		return -1;
	}
	if (blob_len < ALIRO_GCM_TAG_LEN || i + blob_len > sd_len) {
		return -1;
	}
	size_t ct_len = (size_t)blob_len - ALIRO_GCM_TAG_LEN;

	if (ct_len > cap) {
		return -1;
	}
	if (aliro_secchan_open(sc, NULL, 0, sd + i, ct_len, sd + i + ct_len, out) != 0) {
		return -1;
	}
	*out_len = ct_len;
	return 0;
}

/* ---- ENVELOPE / GET RESPONSE APDUs (§8.4.4) ------------------------------ */

/**
 * Encode an APDU ENVELOPE command (ISO 7816-4 chaining flag, INS 0xC3, and 5-byte fixed header)
 * wrapping plaintext data; return 0 on success or -1 if data_len is 0, exceeds 255, or output
 * buffer is too small.
 */
int aliro_stepup_build_envelope(const uint8_t *data, size_t data_len, int chaining, uint8_t *out,
				size_t cap, size_t *out_len)
{
	if (data_len == 0 || data_len > 255u || cap < 5u + data_len + 1u) {
		return -1;
	}
	out[0] = chaining ? 0x10u : 0x00u;
	out[1] = ALIRO_INS_ENVELOPE;
	out[2] = 0x00u;
	out[3] = 0x00u;
	out[4] = (uint8_t)data_len;
	memcpy(out + 5, data, data_len);
	out[5 + data_len] = 0x00u; /* Le = 256 */
	*out_len = 5u + data_len + 1u;
	return 0;
}

/**
 * Encode an APDU GET RESPONSE command (INS 0xC0) with expected response length le; return 0 on
 * success or -1 if output buffer is too small.
 */
int aliro_stepup_build_get_response(uint8_t le, uint8_t *out, size_t cap, size_t *out_len)
{
	if (cap < 5u) {
		return -1;
	}
	out[0] = 0x00u;
	out[1] = ALIRO_INS_GET_RESPONSE;
	out[2] = 0x00u;
	out[3] = 0x00u;
	out[4] = le; /* 0 => up to 256 */
	*out_len = 5u;
	return 0;
}

/* ---- verifier (§7.4) ----------------------------------------------------- */

/* Build the COSE Sig_structure ["Signature1", protected, ext_aad(empty), payload]
 * that the IssuerAuth ES256 signature covers. Returns the length or 0 on error. */
static size_t build_sig_structure(const struct aliro_stepup_doc *doc, uint8_t *out, size_t cap)
{
	struct cw w = {out, out + cap, 0};

	cw_arr(&w, 4);
	cw_tstr(&w, "Signature1");
	cw_bstr(&w, doc->protected_hdr, doc->protected_len);
	cw_bstr(&w, NULL, 0); /* external_aad = empty bstr */
	cw_bstr(&w, doc->payload, doc->payload_len);
	if (w.err) {
		return 0;
	}
	return (size_t)(w.p - out);
}

/* Extract a P-256 end-entity public key from an x5chain: scan for the SPKI
 * uncompressed-point marker `03 42 00 04` (BIT STRING, 66 bytes, 0 unused, 0x04)
 * and take the following 64 bytes as X||Y. Bounded; no full DER parse. */
static int x5chain_ee_pubkey(const uint8_t *x5, size_t n, uint8_t pub[65])
{
	if (x5 == NULL || n < 4u + 64u) {
		return -1;
	}
	for (size_t i = 0; i + 4u + 64u <= n; i++) {
		if (x5[i] == 0x03u && x5[i + 1] == 0x42u && x5[i + 2] == 0x00u &&
		    x5[i + 3] == 0x04u) {
			pub[0] = 0x04u;
			memcpy(pub + 1, x5 + i + 4, 64);
			return 0;
		}
	}
	return -1;
}

/**
 * Select an issuer public key for Step-Up signature verification from x5chain, kid-matched
 * provisioned issuer, or single fallback issuer; set *chain_validated to 0 if x5chain is used (not
 * validated), 1 otherwise; return 0 on success or -1 if selection criteria cannot be met.
 */
static int select_issuer(const struct aliro_stepup_doc *doc,
			 const struct aliro_stepup_verify_ctx *ctx, uint8_t pub[65],
			 int *chain_validated)
{
	*chain_validated = 1;
	if (doc->x5chain != NULL) {
		*chain_validated = 0; /* reference limitation: chain NOT validated */
		return x5chain_ee_pubkey(doc->x5chain, doc->x5chain_len, pub);
	}
	if (doc->kid != NULL && ctx->issuers != NULL) {
		for (size_t i = 0; i < ctx->n_issuers; i++) {
			if (ctx->issuers[i].kid_len == doc->kid_len &&
			    memcmp(ctx->issuers[i].kid, doc->kid, doc->kid_len) == 0) {
				memcpy(pub, ctx->issuers[i].pub, 65);
				return 0;
			}
		}
		return -1;
	}
	/* No kid and no x5chain: only a single provisioned issuer is unambiguous. */
	if (ctx->n_issuers == 1u) {
		memcpy(pub, ctx->issuers[0].pub, 65);
		return 0;
	}
	return -1;
}

/**
 * Search a Step-Up document's digest list for an entry with the given numeric id; return pointer to
 * the digest on success or NULL if not found.
 */
static const struct aliro_stepup_digest *find_digest(const struct aliro_stepup_doc *doc,
						     uint64_t id)
{
	for (size_t i = 0; i < doc->n_digests; i++) {
		if (doc->digests[i].id == id) {
			return &doc->digests[i];
		}
	}
	return NULL;
}

/**
 * Verify a parsed Step-Up document against issuer keys, signature, digests, doctype, time window,
 * and validity iteration; populate verdict struct with per-step validation results and overall
 * validity flag; return 0 if valid, -1 otherwise.
 */
int aliro_stepup_verify(const struct aliro_stepup_doc *doc,
			const struct aliro_stepup_verify_ctx *ctx, struct aliro_stepup_verdict *v)
{
	memset(v, 0, sizeof(*v));
	if (!doc->have_document || doc->n_items == 0) {
		v->reject_step = 0; /* no data elements returned -> reject (§7.4) */
		return -1;
	}

	/* Step 1: issuer-key selection. */
	uint8_t issuer_pub[65];

	v->issuer_key_found = select_issuer(doc, ctx, issuer_pub, &v->issuer_chain_validated) == 0;

	/* Step 2: IssuerAuth ES256 verification. */
	if (v->issuer_key_found && ctx->ecdsa_verify != NULL) {
		uint8_t sig_struct[512];
		size_t ss = build_sig_structure(doc, sig_struct, sizeof(sig_struct));

		v->sig_ok = ss > 0 &&
			    ctx->ecdsa_verify(issuer_pub, sig_struct, ss, doc->signature) == 0;
	}

	/* Step 3: recompute each disclosed item's digest against valueDigests. */
	for (size_t i = 0; i < doc->n_items; i++) {
		const struct aliro_stepup_digest *d = find_digest(doc, doc->items[i].digest_id);
		uint8_t h[32];

		if (d == NULL) {
			continue;
		}
		aliro_sha256(doc->items[i].tagged, doc->items[i].tagged_len, h);
		if (memcmp(h, d->hash, 32) == 0) {
			v->valid_elements++;
		}
	}
	v->digests_ok = v->valid_elements > 0;

	/* Step 4: DocType match (MSO vs Documents vs the requested type). */
	v->doctype_ok = doc->mso_doc_type[0] != '\0' &&
			strcmp(doc->mso_doc_type, doc->doc_type) == 0 &&
			(ctx->expected_doctype == NULL ||
			 strcmp(doc->doc_type, ctx->expected_doctype) == 0);

	/* Step 5: validity window under the TimeVerificationRequired policy (§7.2.4). */
	if (ctx->time_valid) {
		v->time_ok = doc->have_valid_from && doc->have_valid_until &&
			     ctx->now_epoch >= doc->valid_from_epoch &&
			     ctx->now_epoch <= doc->valid_until_epoch;
	} else {
		/* cannot validate time: required -> invalid; else reference treats valid. */
		v->time_ok = !doc->time_verification_required;
	}

	/* Step 6: ValidityIteration (§7.2.3), if present. */
	if (doc->have_iteration && doc->iteration < ctx->access_iteration) {
		v->iteration_ok = (ctx->access_iteration - doc->iteration) < 8u;
	} else {
		v->iteration_ok = 1;
	}

	v->valid = v->issuer_key_found && v->sig_ok && v->digests_ok && v->doctype_ok &&
		   v->time_ok && v->iteration_ok && v->valid_elements > 0;
	v->reject_step = !v->issuer_key_found ? 1
			 : !v->sig_ok         ? 2
			 : !v->digests_ok     ? 3
			 : !v->doctype_ok     ? 4
			 : !v->time_ok        ? 5
			 : !v->iteration_ok   ? 6
					      : 0;
	return v->valid ? 0 : -1;
}

/**
 * Decrypt sessionData from a Step-Up response APDU, parse the plaintext as a document, and verify
 * it; return 0 on success or -1 on decryption failure, parse error, or verification failure.
 */
int aliro_stepup_run(struct aliro_secchan *sc, const uint8_t *sd_resp, size_t sd_len,
		     const struct aliro_stepup_verify_ctx *ctx, uint8_t *scratch,
		     size_t scratch_cap, struct aliro_stepup_doc *doc,
		     struct aliro_stepup_verdict *verdict)
{
	size_t dr_len;

	memset(verdict, 0, sizeof(*verdict));
	if (aliro_stepup_open_sessiondata(sc, sd_resp, sd_len, scratch, scratch_cap, &dr_len) !=
	    0) {
		return -1;
	}
	if (aliro_stepup_parse_response(scratch, dr_len, doc) != 0) {
		return -1;
	}
	return aliro_stepup_verify(doc, ctx, verdict);
}
