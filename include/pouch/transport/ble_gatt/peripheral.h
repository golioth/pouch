/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#define GOLIOTH_BLE_GATT_UUID_SVC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d272)

#define GOLIOTH_BLE_GATT_ADV_DATA \
    BT_DATA_BYTES(BT_DATA_SVC_DATA128, GOLIOTH_BLE_GATT_UUID_SVC_VAL, 0xA5)

struct golioth_ble_gatt_peripheral;

struct golioth_ble_gatt_peripheral *golioth_ble_gatt_peripheral_create(const char *device_id);
int golioth_ble_gatt_peripheral_destroy(struct golioth_ble_gatt_peripheral *peripheral);
