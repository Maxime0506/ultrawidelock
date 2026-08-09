/*
 * OpenAliro hardware adaptation contract.
 *
 * A reader port provides the DW3000 GPIO/IRQ, DW3000 SPI, BLE peripheral,
 * and credential-store functions declared below. An initiator port provides
 * the BLE central transport. Platform-independent helpers declared by
 * aliro_ble_central.h and aliro_prov.h remain implemented in modules/.
 */
#pragma once

#include "aliro_ble.h"
#include "aliro_ble_central.h"
#include "aliro_prov.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"
