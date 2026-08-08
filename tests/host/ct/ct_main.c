/*
 * ct_main.c — the constant-time harness.
 *
 * WHAT THIS TESTS, AND WHAT IT DOES NOT
 *
 * The technique is ctgrind's: mark the secret as *undefined* memory, then run the code under
 * memcheck. Memcheck already reports a conditional jump or a memory index that depends on
 * undefined data — which, when the undefinedness came from a key rather than from a missing
 * initialiser, is exactly the definition of a secret-dependent branch or a secret-dependent
 * table lookup. So a tool built to find uninitialised reads becomes a tool that finds timing
 * side channels, with no new instrumentation.
 *
 * What it covers here: OUR key handling. The CCC ladder in ccc_kdf.c, the STS derivation, the
 * SP0 CCM* wrapper and its MIC verification. If any of those grows a `if (key[i] == ...)`, an
 * early-out compare, or a length that varies with key content, this fails.
 *
 * What it does NOT cover, and the reason is worth stating plainly rather than leaving for
 * someone to discover from a green run: the AES primitive. tests/host/aes_ref.c is an S-box
 * implementation — SBOX[256] indexed by state bytes — so it is variable-time by construction and
 * would drown every run in findings. It is suppressed (tests/host/ct/host-aes.supp).
 *
 * That suppression is honest rather than convenient, because the host AES is not the AES that
 * ships. On nRF5340 the primitive is CryptoCell through PSA; on ESP32 it is mbedTLS over the
 * hardware AES peripheral. Both are somebody else's constant-time claim to make. The host double
 * exists to make the ladder testable, and its timing behaviour has never been part of the
 * product. So the honest scope of this gate is: everything the project wrote, none of the
 * primitive it calls.
 *
 * PUBLIC OUTPUTS
 *
 * Some ladder outputs are published on air: the UWB addresses out of ccc_uad_addresses(), and
 * the SP0 ciphertext and MIC. Values derived from a secret stay poisoned by default, which is
 * right for key material and wrong for these — code is *supposed* to be able to branch on a
 * frame it is about to transmit. Each one is explicitly declassified below, with a note on why,
 * so that "this is public" is a decision recorded in the harness and not an accident of where
 * the poison happened to stop.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ccc_kdf.h"
#include "ccc_mac.h" /* CCC_MHR_LEN, CCC_PRE_POLL_LEN */
#include "ccc_sts.h"

#if defined(CT_POISON)
#include <valgrind/memcheck.h>
#define SECRET(p, n) VALGRIND_MAKE_MEM_UNDEFINED((p), (n))
#define PUBLIC(p, n) VALGRIND_MAKE_MEM_DEFINED((p), (n))
#else
/* Without CT_POISON the harness still builds and runs, so `make ct` on a host with no valgrind
 * at least proves the ladder links and returns 0 — a compile check, not a timing check, and the
 * runner says so rather than reporting a pass. */
#define SECRET(p, n) ((void)(p), (void)(n))
#define PUBLIC(p, n) ((void)(p), (void)(n))
#endif

#define CHECK(expr)                                                                                \
	do {                                                                                       \
		int rc_ = (expr);                                                                  \
		if (rc_ != 0) {                                                                    \
			fprintf(stderr, "ct: %s returned %d\n", #expr, rc_);                       \
			return 1;                                                                  \
		}                                                                                  \
	} while (0)

int main(void)
{
	/* The value does not matter — memcheck tracks definedness, not content, so the ladder runs
	 * over whatever is here while every branch on it is reported. A fixed pattern keeps the run
	 * reproducible if someone builds this without CT_POISON. */
	uint8_t ursk[CCC_URSK_LEN];
	memset(ursk, 0xA5, sizeof(ursk));
	SECRET(ursk, sizeof(ursk));

	uint8_t mupsk1[CCC_MUPSK1_LEN], mupsk2[CCC_MUPSK2_LEN], mursk[CCC_MURSK_LEN];
	uint8_t salted[CCC_SALTED_HASH_LEN], ursk_kt[CCC_URSK_KT_LEN];
	uint8_t dursk[CCC_DURSK_LEN], dudsk[CCC_DUDSK_LEN], sts_v[CCC_STS_V_LEN];
	uint8_t uad[CCC_UAD_LEN];

	/* The ranging configuration is public: it is negotiated in the clear over BLE. Left
	 * defined, so a branch on it is not a finding. */
	static const uint8_t ranging_config[16] = {0};

	CHECK(ccc_derive_mupsk1(ursk, mupsk1));
	CHECK(ccc_derive_mupsk2(ursk, mupsk2));
	CHECK(ccc_derive_mursk(ursk, mursk));
	CHECK(ccc_derive_salted_hash(ursk, ranging_config, sizeof(ranging_config), salted));

	/* The STS index is a counter carried on air. Public. */
	const uint32_t sts_index = 0x0001D4C0u;

	CHECK(ccc_derive_ursk_kt(mursk, sts_index, ursk_kt));
	CHECK(ccc_derive_dursk(ursk_kt, salted, dursk));
	CHECK(ccc_derive_dudsk(ursk_kt, salted, dudsk));
	CHECK(ccc_derive_sts_v(salted, sts_index, sts_v));

	/* ccc_sts_apply writes dURSK and STS-V to the radio. On host the register writes are
	 * no-ops (tests/host/shim), so what is exercised is the marshalling around them — which is
	 * the part that could branch on key bytes while packing them. */
	CHECK(ccc_sts_apply(dursk, sts_v));

	/* --- UWB addresses: derived from a secret, then published -------------------------- */
	CHECK(ccc_derive_uad(mupsk2, sts_index, uad));

	uint8_t keysource[CCC_KEYSOURCE_LEN];
	uint8_t dest_short[CCC_DEST_SHORT_ADDR_LEN];
	uint8_t src_long[CCC_SRC_LONG_ADDR_LEN];
	CHECK(ccc_uad_addresses(uad, keysource, dest_short, src_long));

	/* Declassified: these three go into the MHR of every frame this node transmits, so they
	 * are attacker-visible by design and code downstream may branch on them freely. Note that
	 * ccc_uad_addresses() itself ran with uad still poisoned, which is the check that matters —
	 * the split must not branch on the material it is splitting. */
	PUBLIC(keysource, sizeof(keysource));
	PUBLIC(dest_short, sizeof(dest_short));
	PUBLIC(src_long, sizeof(src_long));

	/* --- SP0: encrypt, then verify ------------------------------------------------------ */
	static const uint8_t mhr[CCC_MHR_LEN] = {0};
	uint8_t payload[CCC_PRE_POLL_LEN];
	memset(payload, 0x5Au, sizeof(payload));

	uint8_t ct[CCC_PRE_POLL_LEN], mic[CCC_SP0_MIC_LEN];
	CHECK(ccc_sp0_encrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr), payload, sizeof(payload), ct,
			      mic));

	/* Declassified: the ciphertext and MIC are transmitted. Without this the decrypt below
	 * would report the MIC comparison as a secret-dependent branch, which would be a false
	 * positive — a receiver compares a tag it was given against one it computed, and the given
	 * one is public. What must still hold is that the comparison does not terminate early on
	 * the SECRET side, and sp0_ct_diff() is what makes that true; this harness is what stops it
	 * quietly reverting to memcmp(). */
	PUBLIC(ct, sizeof(ct));
	PUBLIC(mic, sizeof(mic));

	/* The verification OUTCOME is public — a receiver either acts on the frame or drops it, and
	 * that is observable on air whatever the implementation does. So the return code is
	 * declassified before it is branched on. What is NOT excused by this is the comparison
	 * itself: sp0_ct_diff() must keep accumulating over the whole tag rather than returning
	 * early, and that property is checked inside ccc_sp0_decrypt, above this line. */
	uint8_t pt[CCC_PRE_POLL_LEN];
	int rc = ccc_sp0_decrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr), ct, sizeof(ct), mic, pt);
	PUBLIC(&rc, sizeof(rc));
	CHECK(rc);

	/* Rejection path, which is the one that has historically leaked in CCM* implementations:
	 * a corrupted tag must cost the same as a valid one. */
	uint8_t bad_mic[CCC_SP0_MIC_LEN];
	memcpy(bad_mic, mic, sizeof(bad_mic));
	bad_mic[0] ^= 0xFFu;
	rc = ccc_sp0_decrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr), ct, sizeof(ct), bad_mic, pt);
	PUBLIC(&rc, sizeof(rc));
	if (rc == 0) {
		fprintf(stderr, "ct: a corrupted MIC verified\n");
		return 1;
	}

	/* A CMAC over public data with a secret key — the shape every derivation above reduces to,
	 * exercised once directly so a regression in ccc_aes_cmac() is attributed to ccc_aes_cmac()
	 * rather than to whichever derivation happened to be listed first. */
	uint8_t tag[CCC_CMAC_TAG_LEN];
	CHECK(ccc_aes_cmac(mursk, 256u, ranging_config, sizeof(ranging_config), tag));

	printf("ct: ladder completed\n");
	return 0;
}
