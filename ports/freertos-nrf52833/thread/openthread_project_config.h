/*
 * OpenThread's project configuration for this product.
 *
 * Passed to upstream's CMake as OT_PROJECT_CONFIG, which is the supported way
 * to override src/core/config defaults without patching the tree. It is
 * separate from thread/ot_compat/ultrawidelock_freertos_ot_config.h, which forces Zephyr
 * Kconfig symbols onto the pinned Nordic radio platform: that file configures
 * a vendor source, this one configures OpenThread itself.
 *
 * Only settings that must differ from upstream are here.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H
#define ULTRAWIDELOCK_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H

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

#endif /* ULTRAWIDELOCK_FREERTOS_OPENTHREAD_PROJECT_CONFIG_H */
