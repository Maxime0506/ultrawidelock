// Aliro step-up (Access Document) phase: builds the mdoc DeviceRequest, unwraps and decrypts the
// SessionData DeviceResponse, decodes the CBOR document per spec 7.2/8.4.2, and runs the six-step
// Access Document verification of spec 7.4. Reference-completeness codec + verifier; the verdict is
// logged and stored, never gates the unlock (the provisioned trust store remains the sole gate).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_stepup — the Aliro §8.4 step-up phase: the ISO 18013-5 (mdoc) document
 * exchange the Reader MAY run in the standard phase to obtain an Access or
 * Revocation Document. Three concerns, split across two translation units so the
 * wire-facing decoder fuzzes with no crypto dependency:
 *
 *   aliro_stepup_parse.c  pure CBOR decode + DeviceResponse structural parse
 *                         (Table 7-1/7-2/8-22). No crypto, no allocation; every
 *                         field is a bounds-checked slice of the caller's buffer.
 *   aliro_stepup.c        the DeviceRequest builder, the ENVELOPE / GET RESPONSE
 *                         APDU codec, the SessionData seal/open (reusing the
 *                         aliro_secchan AES-256-GCM channel under StepUpSK), and
 *                         the §7.4 verifier (digest recompute + validity + the
 *                         issuer-signature check via an injected ES256 verify).
 *
 * The ES256 primitive is passed in (aliro_stepup_verify_ctx.ecdsa_verify) so this
 * module carries no elliptic-curve dependency: the target wires the PSA-backed
 * aliro_ecdsa_p256_verify, the host KAT injects its own.
 *
 * Provenance: original. Structures from the Aliro v1.0 spec (§7, §8.4, §14.6)
 * and ISO 18013-5; the code is original.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "aliro_crypto.h" /* struct aliro_secchan */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- document types (§7.7) ---- */
#define ALIRO_STEPUP_DOCTYPE_ACCESS     "aliro-a"
#define ALIRO_STEPUP_DOCTYPE_REVOCATION "aliro-r"

/* ---- StepUpSK-derived session keys (§8.4.3): HKDF-SHA256(empty salt, IKM =
 * StepUpSK, info = "SKReader"/"SKDevice", L = 32). StepUpSK is block[64..95]. */
#define ALIRO_STEPUP_SK_OFFSET 64u
int aliro_stepup_derive_keys(const uint8_t block[ALIRO_KEY_BLOCK_LEN],
			     uint8_t sk_reader[ALIRO_SESSION_KEY_LEN],
			     uint8_t sk_device[ALIRO_SESSION_KEY_LEN]);

/* Initialise a SessionData secure channel: enc = SKReader (reader->device),
 * dec = SKDevice (device->reader). Counters start at 1 (aliro_secchan_init). */
void aliro_stepup_channel_init(struct aliro_secchan *sc,
			       const uint8_t sk_reader[ALIRO_SESSION_KEY_LEN],
			       const uint8_t sk_device[ALIRO_SESSION_KEY_LEN]);

/* ---- requester (§8.4.2) ----
 * Build the fixed Access-Document DeviceRequest CBOR (Table 8-21): docType
 * "aliro-a", namespace "aliro-a", requesting the given element identifiers
 * (intent-to-retain true). With elems=NULL/n_elems=0, requests element2+element4
 * (the §14.6 example request). Returns 0 and sets *out_len, or -1 on overflow. */
int aliro_stepup_build_device_request(const char *const *elems, size_t n_elems, uint8_t *out,
				      size_t cap, size_t *out_len);

/* Seal a DeviceRequest into a SessionData message {"data": bstr(ct||tag)} and
 * advance the channel. Returns 0 and sets *out_len, or -1. */
int aliro_stepup_seal_sessiondata(struct aliro_secchan *sc, const uint8_t *plain, size_t plain_len,
				  uint8_t *out, size_t cap, size_t *out_len);

/* Open a SessionData message: unwrap {"data": bstr}, AES-256-GCM-open under the
 * channel, and write the plaintext DeviceResponse. Returns 0 and sets *out_len;
 * <0 on a malformed wrapper or a GCM tag mismatch. */
int aliro_stepup_open_sessiondata(struct aliro_secchan *sc, const uint8_t *sd, size_t sd_len,
				  uint8_t *out, size_t cap, size_t *out_len);

/* ---- ENVELOPE / GET RESPONSE APDUs (§8.4.4) ----
 * ENVELOPE carries the SessionData in the command data field. chaining=1 sets
 * CLA 0x10 (more blocks follow). Result = "CLA C3 00 00 Lc <data> 00". */
#define ALIRO_INS_ENVELOPE     0xC3u
#define ALIRO_INS_GET_RESPONSE 0xC0u
int aliro_stepup_build_envelope(const uint8_t *data, size_t data_len, int chaining, uint8_t *out,
				size_t cap, size_t *out_len);
/* GET RESPONSE for `le` bytes (from the 61XX status word): "00 C0 00 00 <le>". */
int aliro_stepup_build_get_response(uint8_t le, uint8_t *out, size_t cap, size_t *out_len);

/* ---- parsed DeviceResponse (slices into the caller's buffer) ---- */
#define ALIRO_STEPUP_MAX_DIGESTS 24u
#define ALIRO_STEPUP_MAX_ITEMS   16u
#define ALIRO_STEPUP_ID_MAX      32u

/**
 * Step-up credential element digest: SHA-256 hash of a disclosed IssuerSignedItem, with its
 * digest_id for verification.
 */
struct aliro_stepup_digest {
	uint64_t id;
	uint8_t hash[32];
};

/**
 * Single disclosed IssuerSignedItem from a Step-up document: digest_id (which digest to check
 * against), tagged (24(bstr(IssuerSignedItem)) bytes for hashing), elem_id (element name).
 */
struct aliro_stepup_item {
	uint64_t digest_id;
	const uint8_t
		*tagged; /* the 24(bstr(IssuerSignedItem)) bytes, hashed for the digest check */
	size_t tagged_len;
	char elem_id[ALIRO_STEPUP_ID_MAX];
};

/**
 * Parsed Step-up access document (ISO/IEC 18013-5 mDoc): have_document (0 if device declined),
 * status (DeviceResponse "3" code), doc_type and name_space (issuer namespace), IssuerAuth
 * COSE_Sign1 components (protected header, kid, x5chain, payload=24(bstr(MSO)), signature r||s),
 * MobileSecurityObject (digest algorithm, doc type, disclosed digests), validity dates
 * (signed/valid_from/valid_until as epoch seconds, with flags), and disclosed IssuerSignedItems
 * (elem_id, tagged bytes, digest_id for matching against MSO).
 */
struct aliro_stepup_doc {
	int have_document; /* 0 = DeviceResponse carried no documents (device declined) */
	int status;        /* DeviceResponse "3" */

	char doc_type[ALIRO_STEPUP_ID_MAX];   /* Documents[].docType ("5") */
	char name_space[ALIRO_STEPUP_ID_MAX]; /* the single issuerSigned namespace */

	/* IssuerAuth COSE_Sign1 [protected, unprotected, payload, signature]. */
	const uint8_t *protected_hdr; /* content of the protected bstr */
	size_t protected_len;
	const uint8_t *kid; /* unprotected label 4, or NULL */
	size_t kid_len;
	const uint8_t *x5chain; /* unprotected label 33, or NULL (raw item bytes) */
	size_t x5chain_len;
	const uint8_t *payload; /* content of the payload bstr = 24(bstr(MSO)) */
	size_t payload_len;
	const uint8_t *signature; /* 64-byte r||s */

	/* MobileSecurityObject (Table 7-1). */
	char digest_alg[ALIRO_STEPUP_ID_MAX]; /* "SHA-256" */
	char mso_doc_type[ALIRO_STEPUP_ID_MAX];
	struct aliro_stepup_digest digests[ALIRO_STEPUP_MAX_DIGESTS];
	size_t n_digests;

	/* validityInfo tdates as parsed epoch seconds (UTC). */
	int have_signed, have_valid_from, have_valid_until;
	int64_t signed_epoch, valid_from_epoch, valid_until_epoch;
	int have_iteration;
	uint64_t iteration;
	int time_verification_required;

	/* disclosed IssuerSignedItems. */
	struct aliro_stepup_item items[ALIRO_STEPUP_MAX_ITEMS];
	size_t n_items;
};

/* Structural decode of a plaintext DeviceResponse (Table 8-22). CRYPTO-FREE and
 * bounds-checked: this is the fuzz surface. Returns 0 on a well-formed document
 * (which may still be have_document=0), <0 on malformed CBOR / limits exceeded. */
int aliro_stepup_parse_response(const uint8_t *buf, size_t len, struct aliro_stepup_doc *doc);

/* ---- verifier (§7.4) ---- */
struct aliro_stepup_issuer {
	const uint8_t *kid; /* matched against IssuerAuth kid */
	size_t kid_len;
	uint8_t pub[65]; /* P-256 uncompressed issuer public key */
};

/**
 * Context for Step-up document verification: issuers (trust store), time_valid/now_epoch (clock
 * state), access_iteration (stored iteration for replay check), expected_doctype, ecdsa_verify
 * callback (ES256 over message, takes 65-byte pub, message, 64-byte r||s sig, returns 0 on valid).
 */
struct aliro_stepup_verify_ctx {
	const struct aliro_stepup_issuer *issuers;
	size_t n_issuers;
	int time_valid;            /* reader holds a trusted clock (timesync) */
	int64_t now_epoch;         /* current UTC seconds; used only when time_valid */
	uint64_t access_iteration; /* stored AccessIteration for this issuer (default 0) */
	const char *expected_doctype;
	/* ES256 over msg (hashing internal), pub = 65-byte point, sig = 64-byte r||s.
	 * Returns 0 on a valid signature. Target: aliro_ecdsa_p256_verify. */
	int (*ecdsa_verify)(const uint8_t pub[65], const uint8_t *msg, size_t msg_len,
			    const uint8_t sig[64]);
};

/**
 * Verdict of Step-up document verification (ISO/IEC 18013-5 §7.4): valid (all passed steps + >=1
 * valid element), reject_step (0=accepted, else step 1-6 that failed),
 * issuer_key_found/issuer_chain_validated/sig_ok/digests_ok/doctype_ok/time_ok/iteration_ok
 * (per-step flags), valid_elements (count of disclosed items with verified digest).
 */
struct aliro_stepup_verdict {
	int valid;       /* all applicable steps passed AND >=1 valid element */
	int reject_step; /* 0 = accepted; else the first §7.4 step that failed (1..6) */

	int issuer_key_found;       /* step 1 */
	int issuer_chain_validated; /* 0 for x5chain: EE key used, chain NOT validated (ref limit)
				     */
	int sig_ok;                 /* step 2 */
	int digests_ok;             /* step 3: every disclosed item matched its valueDigest */
	int doctype_ok;             /* step 4 */
	int time_ok;                /* step 5 */
	int iteration_ok;           /* step 6 */
	size_t valid_elements;      /* disclosed items whose digest verified */
};

/* Run §7.4 over a parsed document. Never gates access; fills *verdict for the
 * caller to log/store. Returns 0 if verdict->valid, <0 otherwise (same info as
 * verdict->reject_step; the return is a convenience, not an access decision). */
int aliro_stepup_verify(const struct aliro_stepup_doc *doc,
			const struct aliro_stepup_verify_ctx *ctx,
			struct aliro_stepup_verdict *verdict);

/* Convenience: open the SessionData response, parse, and verify in one call.
 * Fills *verdict; returns 0 on verdict->valid, <0 on any decrypt/parse/verify
 * failure (the worker logs the verdict regardless of the return). scratch must
 * hold the decrypted DeviceResponse (>= sd_len). */
int aliro_stepup_run(struct aliro_secchan *sc, const uint8_t *sd_resp, size_t sd_len,
		     const struct aliro_stepup_verify_ctx *ctx, uint8_t *scratch,
		     size_t scratch_cap, struct aliro_stepup_doc *doc,
		     struct aliro_stepup_verdict *verdict);

/* ---- ESP worker seam (implemented per-platform; see aliro_stepup_worker.c) ----
 * Copies the collected SessionData response + keys + verify inputs and runs
 * aliro_stepup_run() off the BLE-host task, so parse/verify never touches the
 * auth segment or the ranging arm window. Returns 0 if queued. */
struct aliro_stepup_job {
	uint8_t sk_reader[ALIRO_SESSION_KEY_LEN];
	uint8_t sk_device[ALIRO_SESSION_KEY_LEN];
	uint8_t issuer_pub[65];
	uint8_t issuer_kid[16];
	size_t issuer_kid_len;
	int have_issuer;
	int time_valid;
	int64_t now_epoch;
	uint16_t conn_handle;
	size_t sd_len;
	uint8_t sd[2048]; /* SessionData response (x5chain-cert headroom) */
};
int aliro_stepup_worker_submit(const struct aliro_stepup_job *job);

/* Copy out the most recent verdict the worker produced (for `aliro-stepup
 * status`). Returns 1 and fills *verdict (+ *conn if non-NULL) when one exists,
 * 0 otherwise. Implemented in the per-platform worker. */
int aliro_stepup_worker_last(struct aliro_stepup_verdict *verdict, uint16_t *conn);

#ifdef __cplusplus
}
#endif
