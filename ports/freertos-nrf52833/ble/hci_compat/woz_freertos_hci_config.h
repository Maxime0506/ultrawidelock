/*
 * Build configuration for the pinned Nordic opcode dispatcher.
 *
 * The dispatcher selects its command set with Zephyr Kconfig symbols. This
 * port has no Kconfig, so the same symbols are forced onto its command line
 * from here. Every entry below must mirror a controller feature that
 * radio/radio_start_freertos.c actually links with sdc_support_*, otherwise
 * the dispatcher advertises commands the controller will reject. Symbols that
 * stay undefined are the disabled half; do not define any of them to 0.
 */
#ifndef WOZ_FREERTOS_HCI_CONFIG_H
#define WOZ_FREERTOS_HCI_CONFIG_H

/* sdc_support_peripheral: one connection in the peripheral role. */
#define CONFIG_BT_CONN 1
#define CONFIG_BT_PERIPHERAL 1

/* sdc_support_adv: legacy advertising, the only advertising this product uses. */
#define CONFIG_BT_BROADCASTER 1

/* sdc_support_dle_peripheral: 251-byte Link Layer packets for L2CAP CoC throughput. */
#define CONFIG_BT_CTLR_DATA_LENGTH 1

/* sdc_support_le_2m_phy and sdc_support_phy_update_peripheral. */
#define CONFIG_BT_CTLR_PHY 1

/*
 * Always present in the controller, so they need no sdc_support_* call:
 * link encryption for Aliro's secure session, the Filter Accept List the
 * resource configuration already pays for, and the connection RSSI the Aliro
 * approach logic reads.
 */
#define CONFIG_BT_CTLR_LE_ENC 1
#define CONFIG_BT_CTLR_FILTER_ACCEPT_LIST 1
#define CONFIG_BT_CTLR_CONN_RSSI 1

/*
 * CONFIG_BT_HCI_HOST is deliberately left undefined. It is the dispatcher's
 * assertion that the Zephyr host is on the other side of the boundary and
 * therefore never mixes legacy with extended advertising commands. NimBLE is
 * the host here, so the full runtime check has to stay compiled in.
 */

#endif /* WOZ_FREERTOS_HCI_CONFIG_H */
