/*
 * Build configuration for the shared Matter Thread transport.
 *
 * ports/zephyr/matter/matter_thread_port.c is compiled into this image
 * unmodified. Despite where it lives it is not Zephyr code in substance: every
 * line of it is either an OpenThread API call or one of four openthread_*()
 * helpers, and this port already has an exact equivalent of each. Its Zephyr
 * surface is three headers, which matter_compat/ supplies.
 *
 * It is compiled in place rather than copied. A copy of 1,227 lines would drift
 * from the oracle silently, and the SRP behaviour encoded in it -- the host-name
 * suffix that avoids a 14-day key-lease collision, in particular -- was learned
 * the expensive way and should exist once. Its proper home is modules/, beside
 * the protocol it serves; moving it means editing the Zephyr module's
 * CMakeLists, and that build is not gated here, so the move waits until both
 * can be verified in one step. scripts/freertos-matter-source-check.sh fails
 * the build if the file starts using anything this shim does not provide.
 *
 * Symbols that stay undefined are the disabled half. Do not define any of them
 * to 0: the file tests them with #if defined().
 */
#ifndef WOZ_FREERTOS_MATTER_CONFIG_H
#define WOZ_FREERTOS_MATTER_CONFIG_H

/*
 * The transport is built on OpenThread's own API, not Zephyr's L2. The file
 * says so itself: without this it compiles to a set of honest refusals rather
 * than disappearing, because matter_clusters.c calls it unconditionally and a
 * link error would be a worse way to learn Thread was configured out.
 */
#define CONFIG_OPENTHREAD 1

/* Zephyr's levels: 0 none, 1 error, 2 warning, 3 info, 4 debug. */
#define CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL 3

/*
 * Advertised in the commissionable service's "VP" TXT record. Values match the
 * Zephyr oracle's Kconfig defaults, so both images present the same node to a
 * commissioner: 0xFFF1 is CHIP's test vendor, which a phone will commission
 * while marking the result uncertified. A shipping product needs an allocated
 * pair, and changing them here without changing them there would make the two
 * builds answer discovery differently.
 */
#define CONFIG_ALIRO_MATTER_VENDOR_ID 0xFFF1
#define CONFIG_ALIRO_MATTER_PRODUCT_ID 0x8001

/*
 * Deliberately left undefined:
 *
 *   CONFIG_ALIRO_SRP_DIAG   the SRP state-changed diagnostic callback. It is
 *                           the one openthread_*() helper this port has no
 *                           equivalent for -- Zephyr's callback registration
 *                           takes a Zephyr list node -- and it reports on
 *                           registration rather than performing it, so the
 *                           transport is complete without it.
 */

#endif /* WOZ_FREERTOS_MATTER_CONFIG_H */
