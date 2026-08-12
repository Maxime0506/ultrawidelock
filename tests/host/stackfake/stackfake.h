/* stackfake — test-side control/inspection API for the Aliro Interface the
 * ultrawidelock_cred_stack sources call into.
 *
 * SYMMETRIC CRYPTO IS REAL (SHA-256/HKDF from ultrawidelock_hash.c, the reference GCM
 * from ultrawidelock_prim_host.c, both vector-pinned): a wrong key, skipped counter or
 * forged tag fails HERE the way it fails on the device. ASYMMETRIC IS NOT --
 * no P-256 exists in the host build, EC results are knobs, and nothing here is
 * evidence a signature verifies. What that buys: the state machine is checked
 * against real derived key material, and the transport is a recorder
 * (Session::Send keeps every frame).
 */
#ifndef ULTRAWIDELOCK_STACKFAKE_H
#define ULTRAWIDELOCK_STACKFAKE_H

#include <cstddef>
#include <cstdint>

#include <aliro/interface.h>

#define STACKFAKE_MAX_SENDS      32
#define STACKFAKE_SEND_BYTES     1024
#define STACKFAKE_MAX_KEYS       32
#define STACKFAKE_MAX_KPERSIST   8
#define STACKFAKE_MAX_TIMERS     8

/** One frame the stack handed to the transport. */
struct stackfake_send {
	uint8_t bytes[STACKFAKE_SEND_BYTES];
	size_t len;
	bool is_ble;
	uint8_t handle_id;
};

/** A timer slot, as the application's pool would provide. */
struct stackfake_timer {
	bool acquired;
	bool running;
	Aliro::Interface::Os::Timer::Callback callback;
	void *context;
	uint32_t timeout_ms;
};

struct stackfake_state {
	/* ---- transport ----------------------------------------------------- */
	struct stackfake_send sends[STACKFAKE_MAX_SENDS];
	size_t send_count;
	int send_ret;          /**< AliroErrorCode returned by Session::Send */
	int send_fail_in;      /**< -1 never; else fail the Nth send onward */
	unsigned termination_calls;
	unsigned ranging_starts;
	int ranging_ret;
	uint32_t last_ranging_session_id;
	uint8_t last_ursk[32];
	uint16_t last_ranging_protocol_version;

	/* ---- reader identity ----------------------------------------------- */
	int reader_identifier_ret;
	int reader_public_key_ret;
	bool certificate_provisioned;
	int certificate_ret;
	uint8_t certificate[64];
	size_t certificate_len;

	/* ---- access -------------------------------------------------------- */
	int kpersistent_count_ret;
	size_t kpersistent_count;
	int kpersistent_ids_ret;
	int credential_public_key_ret;
	int issuer_public_key_ret;
	int process_access_ret;
	unsigned process_access_calls;
	unsigned process_access_with_document_calls;
	unsigned process_access_fast_calls;
	/* Access-document request: served when want_access_document is set. */
	bool want_access_document;
	uint8_t element_identifier[16];
	size_t element_identifier_len;
	bool intent_to_store;

	/* ---- certificate validation ---------------------------------------- */
	int certificate_validate_ret;
	bool certificate_has_times;
	int certificate_valid_from_year;
	int certificate_valid_until_year;

	/* ---- validity ------------------------------------------------------ */
	bool validity_known;  /**< false: VerifyValidityPeriod returns nullopt */
	bool validity_answer; /**< the answer when known */

	/* ---- crypto knobs (all AliroErrorCode; 0 = success) ---------------- */
	int random_ret;
	int ephemeral_ret;
	int import_shared_ret;
	int import_symmetric_ret;
	int destroy_ret;
	int sign_ret;
	int verify_ret;
	int key_agreement_ret;
	int derive_shared_ret;
	int derive_symmetric_ret;
	int derive_raw_ret;
	int aead_encrypt_ret;
	int aead_decrypt_ret;
	int encrypt_ret;
	int sha256_ret;
	/* Fail the Nth call of a kind, then keep failing. -1 never. */
	int import_symmetric_fail_in;
	int import_shared_fail_in;
	int derive_symmetric_fail_in;

	/* ---- crypto recordings --------------------------------------------- */
	unsigned import_shared_calls, import_symmetric_calls, destroy_calls;
	unsigned aead_encrypt_calls, aead_decrypt_calls, sign_calls, verify_calls;
	unsigned derive_raw_calls, derive_shared_calls, derive_symmetric_calls;
	unsigned sha256_calls, encrypt_calls, key_agreement_calls, ephemeral_calls;
	uint32_t last_aead_encrypt_key, last_aead_decrypt_key;
	uint8_t last_nonce[12];
	size_t last_salt_len;
	uint8_t last_salt[512];
	/* The FIRST DeriveRawKey salt since reset. The fast phase derives
	 * before the volatile schedule does, so this is the one a suite needs
	 * to reproduce the cryptogram key -- last_salt has been overwritten by
	 * the volatile derivation by the time the flow finishes. */
	size_t first_salt_len;
	uint8_t first_salt[512];
	uint8_t last_info[32];
	size_t last_info_len;

	/* Next key id handed out by an import or derive. Ids are never 0: the
	 * stack uses 0 to mean "no key", and DestroyKey() skips it. */
	uint32_t next_key_id;

	/* ---- os ------------------------------------------------------------ */
	struct stackfake_timer timers[STACKFAKE_MAX_TIMERS];
	unsigned timer_acquire_calls, timer_release_calls;
	unsigned timer_start_calls, timer_stop_calls;
	bool timers_exhausted; /**< knob: Acquire always fails */
	int queue_event_ret;
	unsigned queue_event_calls;
	void *last_event;      /**< the event QueueEvent was handed */
	bool queue_event_keeps; /**< true: the fake takes ownership, suite runs it */
	unsigned mutex_lock_depth, mutex_max_depth;

	/* ---- uwb ----------------------------------------------------------- */
	int uwb_handle_ret;
	unsigned uwb_handle_calls;
	uint8_t last_uwb_message[256];
	size_t last_uwb_message_len;
};

extern struct stackfake_state stackfake;

/** @brief Zero every recording and restore every knob to "works". */
void stackfake_reset(void);

/** @brief The Nth frame handed to the transport, or NULL. */
const struct stackfake_send *stackfake_sent(size_t index);

/** @brief The most recent frame handed to the transport, or NULL. */
const struct stackfake_send *stackfake_last_send(void);

/**
 * @brief Create @p count real persistent credential keys for the fast path.
 *
 * Real, because the fast phase derives from them with real HKDF: an id with no
 * key behind it would fail derivation instead of failing the cryptogram, which
 * is a different test. Reach them with stackfake_key_nth("kpersistent", i).
 */
void stackfake_set_kpersistent(size_t count);

/** @brief Fire the callback of the timer holding @p handle, as expiry would. */
void stackfake_fire_timer(int handle);

/* ---- acting as the User Device --------------------------------------------
 *
 * The reader derives its session keys inside session.cpp. The real peer holds
 * the same keys, so a suite that wants to answer as the peer needs to reach
 * them -- not to forge anything, but to seal a response with the key the
 * device would genuinely have. The key table below is that reach.
 *
 * Keys are labelled with what the Interface call could honestly know:
 *   "shared"     an ImportSharedKey (Kdh, StepUpSK, BleSK)
 *   "symmetric"  an ImportSymmetricKey (the two expedited directional keys)
 *   <info>       a DeriveSymmetricKey, labelled with its HKDF info string --
 *                "SKReader", "SKDevice", "BleSKReader", "BleSKDevice"
 */

/** @brief Key id of the @p index'th key created with @p label, or 0. */
uint32_t stackfake_key_nth(const char *label, size_t index);

/** @brief Copy the 32 key bytes behind @p key_id; false if unknown. */
bool stackfake_key_bytes(uint32_t key_id, uint8_t out[32]);

/** @brief How many keys exist, for a suite that wants to assert on churn. */
size_t stackfake_key_count(void);

/**
 * @brief Seal @p plaintext as the User Device would, with real AES-256-GCM.
 *
 * Writes ciphertext followed by its 16-byte tag and returns the total. The
 * nonce is built the way the protocol builds it -- byte 7 marks the device
 * direction, bytes 8..11 carry the counter big-endian -- so a suite that
 * passes the wrong counter produces a response the reader legitimately
 * refuses. Returns 0 if @p key_id is unknown.
 */
size_t stackfake_seal_as_device(uint32_t key_id, uint32_t counter, const uint8_t *aad,
				size_t aad_length, const uint8_t *plaintext, size_t length,
				uint8_t *out);

#endif /* ULTRAWIDELOCK_STACKFAKE_H */
