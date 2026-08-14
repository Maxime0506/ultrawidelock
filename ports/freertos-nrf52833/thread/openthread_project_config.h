/*
 * OpenThread's project configuration for this product.
 *
 * Passed to upstream's CMake as OT_PROJECT_CONFIG, which is the supported way
 * to override src/core/config defaults without patching the tree. It is
 * separate from thread/ot_compat/woz_freertos_ot_config.h, which forces Zephyr
 * Kconfig symbols onto the pinned Nordic radio platform: that file configures
 * a vendor source, this one configures OpenThread itself.
 *
 * Only settings that must differ from upstream are here.
 */
#ifndef WOZ_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H
#define WOZ_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H

/*
 * This port owns Mbed TLS, so OpenThread must not also manage it.
 *
 * With this on, ot::Crypto::MbedTls's constructor calls
 * mbedtls_platform_set_calloc_free() to route the library's allocation through
 * OpenThread's own heap. Two things are wrong with that here. The first is
 * mechanical: crypto/mbedtls_config_freertos.h sets the allocator at compile
 * time with MBEDTLS_PLATFORM_CALLOC_MACRO, so the runtime setter is not
 * compiled and the call does not resolve. The second is the reason the
 * compile-time form was chosen -- the allocator is the FreeRTOS heap, which
 * NimBLE's porting layer already forces the image to carry, and having a second
 * heap inside OpenThread purely for Mbed TLS would mean two pools sized by
 * guesswork instead of one sized by measurement.
 *
 * crypto/crypto_init_freertos.c initialises the library, including the PSA
 * core, before anything else touches it.
 */
#define OPENTHREAD_CONFIG_ENABLE_BUILTIN_MBEDTLS_MANAGEMENT 0

/*
 * NO TCP. Upstream defaults OPENTHREAD_CONFIG_TCP_ENABLE to ON, and this file
 * takes upstream defaults for everything it does not name -- so leaving it out
 * was a choice by omission, which is the kind this port keeps paying for.
 *
 * The oracle turns it off (its .config carries
 * "# CONFIG_OPENTHREAD_TCP_ENABLE is not set"), and it is right to: Matter over
 * Thread is UDP, the SRP client is UDP, and a lock MTD has no TCP caller
 * anywhere. What the default was buying is tcplp, OpenThread's TCP-lite stack,
 * MEASURED at 17,715 bytes of flash in this image's linker map before it was
 * turned off.
 *
 * Found by a review comparing this port's config against the oracle's, not by
 * anything in this build complaining. Nothing would have: an unreachable TCP
 * stack links quietly.
 */
#define OPENTHREAD_CONFIG_TCP_ENABLE 0

/*
 * The microsecond alarm stays at upstream's default of off, which
 * thread/ot_alarm_freertos.c relies on: it implements the millisecond alarm
 * only, and make freertos-radio-source-check fails if that default ever
 * changes. Stated here as a comment rather than a define because the file it
 * lives in tests it with #if and upstream already answers zero.
 */

/*
 * The SRP client, which is how a Matter node becomes findable on Thread.
 *
 * The oracle turns this on as CONFIG_OPENTHREAD_SRP_CLIENT in
 * overlay-thread.conf. Without it the shared Matter transport does not link --
 * otSrpClientAddService, otSrpClientRemoveService and the auto-start entry
 * points are simply absent -- and if it somehow did, the node would attach to
 * Thread, hold a fabric, and never publish a service for the controller to
 * find. That failure looks like an accessory that commissions and then goes
 * missing, which is the expensive kind.
 *
 * ECDSA comes with it and is not optional: the SRP client signs its
 * registrations, and name ownership on the border router is by KEY. The
 * host-name suffix logic in the shared transport exists precisely because that
 * key outlives a name.
 *
 * Only defined for a build that has Matter. A reader-only image has nothing to
 * publish, and this is several kilobytes of flash on a part that has none to
 * spare.
 */
#if defined(WOZ_HAVE_MATTER) && WOZ_HAVE_MATTER
#define OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE 1
#define OPENTHREAD_CONFIG_ECDSA_ENABLE 1

/*
 * ECDSA THROUGH PSA, NOT THROUGH MBED TLS'S PK LAYER.
 *
 * This selects the second half of crypto_platform.cpp -- the branch at its
 * OPENTHREAD_CONFIG_CRYPTO_LIB_PSA guard -- so otPlatCryptoEcdsa* is
 * implemented with psa_sign_hash and friends rather than mbedtls_pk_parse_key
 * and mbedtls_ecdsa_sign_det_ext.
 *
 * The port still implements none of otPlatCrypto: OpenThread compiles the PSA
 * branch itself, exactly as it compiled the Mbed TLS one. What changes is which
 * library it lands on, and that is worth 9,690 bytes of flash -- the PK, ECP,
 * BIGNUM, ASN1 and OID modules stop being linked, while the P-256 arithmetic
 * this image already carried for the reader is reused.
 *
 * KEY REFERENCES are what make it persistent. Without them OpenThread would
 * hold the SRP key as bytes and this would be a pure code-size trade; with them
 * the key lives in PSA storage, which is crypto/psa_its_freertos.c over the
 * port's key-value store. That matters beyond size: name ownership on a border
 * router is by KEY, so an SRP key that did not survive a reboot would make the
 * node ask for a name the server still holds under the old one -- refused for
 * the length of the key lease, with no symptom but a node that attaches to
 * Thread and never registers.
 */
#define OPENTHREAD_CONFIG_CRYPTO_LIB OPENTHREAD_CONFIG_CRYPTO_LIB_PSA
#define OPENTHREAD_CONFIG_PLATFORM_KEY_REFERENCES_ENABLE 1

/*
 * The KeyManager half of exportable MAC keys. See
 * thread/ot_compat/woz_freertos_ot_config.h for why the radio needs the literal
 * bytes at all; this line is what lets it have them. KeyManager reads it as
 * kExportableMacKeys and imports the MAC keys with PSA_KEY_USAGE_EXPORT, and
 * without it the export the radio performs is refused by PSA at the policy
 * check rather than at compile time.
 *
 * Both symbols are set together and neither is useful alone. Zephyr expresses
 * that with a single Kconfig option that feeds both; here it is two defines in
 * two files, which is the cost of not having Kconfig.
 */
#define OPENTHREAD_CONFIG_PLATFORM_MAC_KEYS_EXPORTABLE_ENABLE 1
#endif

#endif /* WOZ_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H */
