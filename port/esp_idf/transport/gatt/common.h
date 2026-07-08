/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pouch GATT UUID definitions for ESP-IDF (NimBLE)
 *
 * NimBLE stores 128-bit UUIDs in little-endian byte order. To convert a
 * standard UUID string to the byte array below, write it as a continuous hex
 * string and reverse the byte order. For example:
 *
 *   UUID:  89a316ae-89b7-4ef6-b1d3-5c9a6e27d272
 *   Hex:   89 a3 16 ae 89 b7 4e f6 b1 d3 5c 9a 6e 27 d2 72
 *   LE:    72 d2 27 6e 9a 5c d3 b1 f6 4e b7 89 ae 16 a3 89
 */

#pragma once

#include "host/ble_uuid.h"

#include <pouch/transport/bluetooth/gatt.h>

#define BT_ATT_OVERHEAD 3 /* opcode (1) + handle (2) */

/* clang-format off */

/* Pouch GATT Service UUID: 0xFC49 */
#define POUCH_GATT_UUID_SVC_VAL BLE_UUID16_INIT(POUCH_GATT_UUID_SVC_VAL_16)

/* Pouch Uplink Characteristic UUID: 89a316ae-89b7-4ef6-b1d3-5c9a6e27d273 */
#define POUCH_GATT_UUID_UPLINK_CHRC_VAL \
    BLE_UUID128_INIT(0x73, 0xd2, 0x27, 0x6e, 0x9a, 0x5c, 0xd3, 0xb1, \
                     0xf6, 0x4e, 0xb7, 0x89, 0xae, 0x16, 0xa3, 0x89)

/* Pouch Downlink Characteristic UUID: 89a316ae-89b7-4ef6-b1d3-5c9a6e27d274 */
#define POUCH_GATT_UUID_DOWNLINK_CHRC_VAL \
    BLE_UUID128_INIT(0x74, 0xd2, 0x27, 0x6e, 0x9a, 0x5c, 0xd3, 0xb1, \
                     0xf6, 0x4e, 0xb7, 0x89, 0xae, 0x16, 0xa3, 0x89)

/* Pouch Info Characteristic UUID: 89a316ae-89b7-4ef6-b1d3-5c9a6e27d275 */
#define POUCH_GATT_UUID_INFO_CHRC_VAL \
    BLE_UUID128_INIT(0x75, 0xd2, 0x27, 0x6e, 0x9a, 0x5c, 0xd3, 0xb1, \
                     0xf6, 0x4e, 0xb7, 0x89, 0xae, 0x16, 0xa3, 0x89)

/* Pouch Server Certificate Characteristic UUID: 89a316ae-89b7-4ef6-b1d3-5c9a6e27d276 */
#define POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL \
    BLE_UUID128_INIT(0x76, 0xd2, 0x27, 0x6e, 0x9a, 0x5c, 0xd3, 0xb1, \
                     0xf6, 0x4e, 0xb7, 0x89, 0xae, 0x16, 0xa3, 0x89)

/* Pouch Device Certificate Characteristic UUID: 89a316ae-89b7-4ef6-b1d3-5c9a6e27d277 */
#define POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL \
    BLE_UUID128_INIT(0x77, 0xd2, 0x27, 0x6e, 0x9a, 0x5c, 0xd3, 0xb1, \
                     0xf6, 0x4e, 0xb7, 0x89, 0xae, 0x16, 0xa3, 0x89)

/* clang-format on */
