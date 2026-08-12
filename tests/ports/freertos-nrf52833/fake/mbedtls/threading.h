/*
 * Mbed TLS's threading surface, cut down to what the port implements.
 *
 * The include of "threading_alt.h" is copied deliberately from the real
 * header: that spelling is the whole reason the port's mutex type has to be in
 * a file of that exact name, and a fake that included it by any other path
 * would not prove the arrangement works.
 */
#ifndef TEST_MBEDTLS_THREADING_H
#define TEST_MBEDTLS_THREADING_H

#define MBEDTLS_ERR_THREADING_BAD_INPUT_DATA -0x001C
#define MBEDTLS_ERR_THREADING_MUTEX_ERROR    -0x001E

#include "threading_alt.h"

void mbedtls_threading_set_alt(void (*mutex_init)(mbedtls_threading_mutex_t *),
			       void (*mutex_free)(mbedtls_threading_mutex_t *),
			       int (*mutex_lock)(mbedtls_threading_mutex_t *),
			       int (*mutex_unlock)(mbedtls_threading_mutex_t *));

/* Recording side, for the test only. */
struct fake_threading_callbacks {
	void (*init)(mbedtls_threading_mutex_t *);
	void (*free)(mbedtls_threading_mutex_t *);
	int (*lock)(mbedtls_threading_mutex_t *);
	int (*unlock)(mbedtls_threading_mutex_t *);
};

extern unsigned fake_threading_set_alt_calls;

const struct fake_threading_callbacks *fake_threading_installed(void);
void fake_threading_reset(void);

#endif /* TEST_MBEDTLS_THREADING_H */
