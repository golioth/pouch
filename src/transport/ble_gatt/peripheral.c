/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/ble_gatt/peripheral.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

static const struct bt_uuid_16 golioth_ble_gatt_svc_uuid =
    BT_UUID_INIT_16(GOLIOTH_BLE_GATT_UUID_SVC_VAL_16);

GOLIOTH_BLE_GATT_SERVICE(&golioth_ble_gatt_svc_uuid);

static struct bt_gatt_service golioth_svc = {
    .attrs = GOLIOTH_BLE_GATT_ATTR_ARRAY_PTR,
};

int golioth_ble_gatt_peripheral_init(void)
{
    GOLIOTH_BLE_GATT_ATTR_ARRAY_LEN(&golioth_svc.attr_count);

    return bt_gatt_service_register(&golioth_svc);
}
