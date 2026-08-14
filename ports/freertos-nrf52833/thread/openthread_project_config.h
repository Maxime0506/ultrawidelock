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
#endif

#endif /* WOZ_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H */
