/* stackfake — test-side control/inspection API for the Aliro Interface the
 * woz_aliro_stack sources call into.
 *
 * THERE IS NO CRYPTOGRAPHY HERE, and that shapes what any suite built on it
 * can claim. Key agreement returns filler. HKDF returns a counter pattern.
 * AES-GCM "encryption" copies the plaintext and writes a tag derived from the
 * key id and nonce, so decryption of the fake's own ciphertext round-trips and
 * anything else fails -- which is enough to drive both branches, and is not
 * evidence that the real AEAD is used correctly. ECDSA verify is a knob.
 *
 * WHAT IT IS GOOD FOR is the state machine, which is the bulk of session.cpp:
 * which command is built next, which key id is used for which direction, which
 * counter advances, when a session is torn down, and what the peer is told.
 * Those are decisions the file makes on its own, and every one of them is
 * observable here.
 *
 * The transport side is a recorder: Session::Send keeps every frame, so a
 * suite reads back exactly what would have gone on the wire.
 */
#ifndef WOZ_STACKFAKE_H
#define WOZ_STACKFAKE_H

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
	/* Accept any tag on decrypt. A response the STACK produced carries a
	 * tag this fake can recompute, but one a suite hand-builds cannot --
	 * the key id it was "encrypted" under is chosen inside session.cpp and
	 * is not visible from outside. With this set a suite scripts a device
	 * response as plaintext plus sixteen filler bytes and still drives the
	 * real state machine; last_aead_decrypt_key still records WHICH key the
	 * stack chose, which is the decision actually under test. Left clear,
	 * the tag is checked, and one case does exactly that. */
	bool aead_decrypt_ignore_tag;
	int encrypt_ret;
	int sha256_ret;
	/* Serve this digest instead of the fake's own mixing function. The
	 * access-document check compares a hash of the issuer-signed item
	 * against a digest recorded inside the document, and no stand-in is
	 * going to reproduce a real SHA-256. A suite parses the document with
	 * the real parser, reads the digest it expects, and installs it here --
	 * so the comparison is exercised, and it is worth nothing as evidence
	 * about the hash itself. */
	bool sha256_forced;
	uint8_t sha256_force[32];
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

/** @brief Install @p count persistent credential key ids for the fast path. */
void stackfake_set_kpersistent(const uint32_t *ids, size_t count);

/** @brief Fire the callback of the timer holding @p handle, as expiry would. */
void stackfake_fire_timer(int handle);

/**
 * @brief Build the tag the fake's AeadEncrypt would produce.
 *
 * A suite uses this to forge a message the stack will accept, which is the
 * only way to drive the encrypted half of the BLE state machine. It is
 * arithmetic over the key id and nonce, not a MAC.
 */
void stackfake_tag(uint32_t key_id, const uint8_t *nonce, uint8_t *tag_out);

#endif /* WOZ_STACKFAKE_H */
