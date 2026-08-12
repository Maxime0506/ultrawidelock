/*
 * Build configuration for the pinned Nordic OpenThread radio platform.
 *
 * radio_nrf5.c selects its behaviour with Zephyr Kconfig symbols. This port
 * has no Kconfig, so the same symbols are forced onto its command line from
 * here. Values match what the NCS v3.3.0 Zephyr oracle resolves for this
 * product, so the two builds drive the 802.15.4 driver identically.
 *
 * Symbols that stay undefined are the disabled half. Do not define any of them
 * to 0: the file tests most of them with #if defined().
 */
#ifndef ULTRAWIDELOCK_FREERTOS_OT_CONFIG_H
#define ULTRAWIDELOCK_FREERTOS_OT_CONFIG_H

/* nRF52833 is little-endian, and the frame helpers branch on this. */
#define CONFIG_LITTLE_ENDIAN 1

/*
 * The receive pool. Each buffer is a full 127-byte frame plus its metadata, so
 * this is the single largest RAM cost of the radio platform. Upstream default.
 */
#define CONFIG_NRF_802154_RX_BUFFERS 20

/*
 * Radio defaults reported to OpenThread. Zero dBm is the Nordic sample default
 * and -100 dBm is upstream's receive sensitivity for this part.
 */
#define CONFIG_OPENTHREAD_DEFAULT_TX_POWER 0
#define CONFIG_OPENTHREAD_DEFAULT_RX_SENSITIVITY (-100)

/*
 * Clock accuracy in ppm claimed for delayed transmit and receive. Nordic's own
 * default, chosen against power consumption rather than the crystal datasheet.
 */
#define CONFIG_NRF5_DELAY_TRX_ACC 20

/*
 * The worker priority only feeds an assertion about the context frames are
 * handed over in. This port runs OpenThread on one cooperative-equivalent
 * FreeRTOS task, which is what the Zephyr build's cooperative range means.
 */
#define CONFIG_COOP_ENABLED 1
#define CONFIG_OPENTHREAD_THREAD_PRIORITY 8

/* Everything the platform logs goes through the port's log sink. */
#define CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL 3

/*
 * Deliberately left undefined, each for a reason:
 *
 *   CONFIG_NRF_802154_SER_HOST            the driver is in this image, not
 *                                         behind a serialization transport
 *   CONFIG_NRF_802154_CALLBACKS_DISPATCHER only OpenThread consumes the
 *                                         driver callouts here
 *   CONFIG_OPENTHREAD_CSL_RECEIVER        this product is a receiver-on MED,
 *                                         not a sleepy end device
 *   CONFIG_OPENTHREAD_TIME_SYNC           no network time service
 *   CONFIG_OPENTHREAD_DIAG                no factory diagnostics command set
 *   CONFIG_OPENTHREAD_NAT64_TRANSLATOR    an MTD does not translate
 *   CONFIG_OPENTHREAD_LINK_METRICS_SUBJECT a subject is a parent-side role
 *   CONFIG_OPENTHREAD_THREAD_VERSION_1_1  the stack is 1.3 or later, which
 *                                         sizes the ack buffer for enhanced acks
 *   CONFIG_OPENTHREAD_WAKEUP_END_DEVICE   not a wake-up end device
 *   CONFIG_NRF5_UICR_EUI64_ENABLE         the EUI-64 comes from FICR
 *   CONFIG_NRF5_VENDOR_OUI_ENABLE         with it, from Nordic's own OUI
 *   CONFIG_NRF5_CARRIER_FUNCTIONS         no continuous carrier test modes
 *   CONFIG_NRF5_SELECTIVE_TXCHANNEL       no per-channel transmit power table
 *   CONFIG_NRF5_LOG_RX_FAILURES           receive failures are counted, not logged
 *   CONFIG_TRUSTED_EXECUTION_NONSECURE    nRF52833 has no secure split
 *   CONFIG_SOC_NRF5340_CPUAPP             nor is it a 5340 or a 54L
 *   CONFIG_SOC_SERIES_NRF54L
 */

#endif /* ULTRAWIDELOCK_FREERTOS_OT_CONFIG_H */
