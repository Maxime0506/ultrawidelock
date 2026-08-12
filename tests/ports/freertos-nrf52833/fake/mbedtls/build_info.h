/*
 * Stand-in for Mbed TLS's build_info.h.
 *
 * On the target this header is what reads MBEDTLS_CONFIG_FILE and then derives
 * the legacy module set from the PSA_WANT_* list. Neither of those matters to
 * the port sources under test here -- they call four threading callbacks, an
 * allocator and an entropy poll, none of which changes shape with the config.
 * What the real header being absent WOULD hide is a config-file mistake, and
 * that is checked instead by freertos-crypto-source-check.sh against the
 * pinned Mbed TLS tree, where the real header is.
 */
#ifndef TEST_MBEDTLS_BUILD_INFO_H
#define TEST_MBEDTLS_BUILD_INFO_H

#define MBEDTLS_VERSION_MAJOR 3
#define MBEDTLS_VERSION_MINOR 6

/*
 * The port's real config file, not a copy of it. On the target the library
 * reaches it through MBEDTLS_CONFIG_FILE from this same header, so pulling it
 * in here keeps the one thing that matters faithful: the allocator
 * declarations that MBEDTLS_PLATFORM_CALLOC_MACRO and _FREE_MACRO name reach
 * every translation unit through this path and no other. It also means the
 * host build compiles the config file, so a mistake in it is a compile error
 * here rather than a surprise at first link.
 */
#include "mbedtls_config_freertos.h"

#endif /* TEST_MBEDTLS_BUILD_INFO_H */
