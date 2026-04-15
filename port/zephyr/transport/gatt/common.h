/*
 * Copyright (c) 2024 Golioth
 */

#pragma once

#include <zephyr/bluetooth/uuid.h>
#include <pouch/transport/bluetooth/gatt.h>

#define BT_ATT_OVERHEAD 3 /* opcode (1) + handle (2) */

#define POUCH_GATT_UUID_SVC_VAL_128 \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d272)

#define POUCH_GATT_UUID_UPLINK_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d273)
#define POUCH_GATT_UUID_DOWNLINK_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d274)
#define POUCH_GATT_UUID_INFO_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d275)
#define POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d276)
#define POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d277)
