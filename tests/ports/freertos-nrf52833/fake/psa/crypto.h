/*
 * The single PSA entry point the port's own crypto sources call.
 *
 * Nothing else of PSA is modelled here on purpose. The port does not wrap the
 * PSA API -- modules/woz_aliro/src/aliro_prim_psa.c calls it directly and is
 * covered by the shared host suite against tests/host/psafake. All this port
 * adds is the bring-up, so this fake only has to be able to succeed, to fail,
 * and to say whether the threading callbacks were installed before it ran.
 */
#ifndef TEST_PSA_CRYPTO_H
#define TEST_PSA_CRYPTO_H

#include <stdbool.h>

typedef int psa_status_t;

#define PSA_SUCCESS                 0
#define PSA_ERROR_INSUFFICIENT_MEMORY (-141)

psa_status_t psa_crypto_init(void);

/* Recording side, for the test only. */
extern unsigned fake_psa_init_calls;

void fake_psa_reset(void);
void fake_psa_set_init_status(psa_status_t status);

/*
 * Whether the threading callbacks were already installed the last time
 * psa_crypto_init() ran. The real core takes its key-slot and RNG mutexes on
 * that path, so an image that got the order wrong would fault on hardware and
 * pass any test that only checked both calls happened.
 */
bool fake_psa_threading_was_ready(void);

#endif /* TEST_PSA_CRYPTO_H */
