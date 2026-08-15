/* SPDX-License-Identifier: ISC */

/*
 * <psa/error.h>, which the PSA Storage API expects to exist and Mbed TLS does
 * not ship.
 *
 * In the PSA specifications the status type and its codes are shared between
 * the crypto and storage APIs, and each is allowed to publish them under its
 * own header name. Mbed TLS publishes them from psa/crypto_types.h and
 * psa/crypto_values.h; the storage API spells the same thing psa/error.h.
 *
 * So this forwards rather than defines. Redeclaring PSA_SUCCESS and the error
 * codes here would put a second set of definitions in the image whose only job
 * is to agree with the first, and the day they stopped agreeing the compiler
 * would say so about a constant rather than about the mistake.
 */
#ifndef ULTRAWIDELOCK_PSA_ERROR_H
#define ULTRAWIDELOCK_PSA_ERROR_H

#include <psa/crypto_types.h>
#include <psa/crypto_values.h>

#endif /* ULTRAWIDELOCK_PSA_ERROR_H */
