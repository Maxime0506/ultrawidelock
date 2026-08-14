/*
 * Zephyr's settings header, forwarding to the port's own.
 *
 * The shared Matter sources include <zephyr/settings/settings.h> because that
 * is where the API they were written against lives. This file exists only to
 * make that spelling resolve; every declaration is in
 * ports/freertos-nrf52833/matter/matter_settings_freertos.h, and the
 * implementation is that header's .c.
 *
 * Same direction as matter_ble_zephyr.h beside it. Port code includes the
 * native header directly and never this one, so nothing under
 * ports/freertos-nrf52833/ carries a Zephyr include -- only this shim
 * directory does, which is its entire job.
 *
 * If the shared sources are ever moved to modules/ this file disappears and
 * their include becomes the native name on both sides.
 */
#ifndef WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H
#define WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H

#include "matter_settings_freertos.h"

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_SETTINGS_H */
