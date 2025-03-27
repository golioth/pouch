/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/bluetooth/uuid.h>

#define GOLIOTH_BLE_GATT_UUID_UPLINK_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d273)
#define GOLIOTH_BLE_GATT_UUID_DOWNLINK_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d274)
#define GOLIOTH_BLE_GATT_UUID_INFO_CHRC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d275)
