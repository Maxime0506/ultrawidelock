/*
 * UltraWideLock hardware adaptation contract.
 *
 * A reader port provides the DW3000 GPIO/IRQ, DW3000 SPI, BLE peripheral,
 * and credential-store functions declared below. An initiator port provides
 * the BLE central transport. Platform-independent helpers declared by
 * ultrawidelock_ble_central.h and ultrawidelock_prov.h remain implemented in modules/.
 */
#pragma once

#include "ultrawidelock_ble.h"
#include "ultrawidelock_ble_central.h"
#include "ultrawidelock_prov.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"
