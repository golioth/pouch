/*
 * Copyright (c) 2024 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/ble_gatt/common/uuids.h>

#define GOLIOTH_BLE_GATT_ADV_DATA \
    BT_DATA_BYTES(BT_DATA_SVC_DATA128, GOLIOTH_BLE_GATT_UUID_SVC_VAL, 0xA5)

struct golioth_ble_gatt_peripheral;

struct golioth_ble_gatt_peripheral *golioth_ble_gatt_peripheral_create(const char *device_id);
int golioth_ble_gatt_peripheral_destroy(struct golioth_ble_gatt_peripheral *peripheral);
