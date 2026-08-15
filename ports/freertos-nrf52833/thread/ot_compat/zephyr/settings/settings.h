/* SPDX-License-Identifier: ISC */

/*
 * One declaration, for one caller.
 *
 * Nordic's crypto_psa.c includes <zephyr/settings/settings.h> and uses exactly
 * settings_subsys_init(), inside an __ASSERT_EVAL, to make sure the settings
 * backend is up before PSA stores a persistent key. It touches nothing else in
 * that API.
 *
 * This is deliberately NOT matter_compat's settings header, and ot_compat is
 * deliberately not extended with matter_compat's include path. Both shim trees
 * publish zephyr/kernel.h and zephyr/logging/log.h; adding the second one to
 * this target's include path shadowed the first and broke radio_nrf5.c, a file
 * that had compiled unchanged for months. A shim tree should be the only one a
 * given target can see.
 *
 * The definition is the port's, in matter/matter_settings_freertos.c, and it
 * returns success because the key-value store is brought up by the board long
 * before any of this runs.
 */
#ifndef ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_SETTINGS_H
#define ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_SETTINGS_H

int settings_subsys_init(void);

#endif /* ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_SETTINGS_H */
