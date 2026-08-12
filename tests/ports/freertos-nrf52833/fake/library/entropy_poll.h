/*
 * The prototype Mbed TLS requires mbedtls_hardware_poll() to match.
 *
 * This is the one declaration in the crypto backend that the port cannot
 * simply write for itself: MBEDTLS_ENTROPY_HARDWARE_ALT works by the library
 * calling a function of this name, and a signature that differs would compile
 * on its own and link to nothing on the target. Copied verbatim from
 * mbedtls/include/library/entropy_poll.h, and asserted against the pinned tree
 * by freertos-crypto-source-check.sh.
 */
#ifndef TEST_MBEDTLS_ENTROPY_POLL_H
#define TEST_MBEDTLS_ENTROPY_POLL_H

#include <stddef.h>

int mbedtls_hardware_poll(void *data,
                          unsigned char *output, size_t len, size_t *olen);

#endif /* TEST_MBEDTLS_ENTROPY_POLL_H */
