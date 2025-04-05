/*
 * Copyright (c) 2024 Golioth
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/ble_gatt/peripheral.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

struct golioth_ble_gatt_peripheral
{
    const char *device_id;
};

static const struct bt_uuid_128 golioth_ble_gatt_svc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_SVC_VAL);

GOLIOTH_BLE_GATT_SERVICE(&golioth_ble_gatt_svc_uuid);

static struct bt_gatt_service golioth_svc = {
    .attrs = GOLIOTH_BLE_GATT_ATTR_ARRAY_PTR,
};

void golioth_ble_gatt_int_peripheral_get_device_id(
    const struct golioth_ble_gatt_peripheral *peripheral,
    const char **device_id)
{
    *device_id = peripheral->device_id;
}

struct golioth_ble_gatt_peripheral *golioth_ble_gatt_peripheral_create(const char *device_id)
{
    struct golioth_ble_gatt_peripheral *peripheral =
        malloc(sizeof(struct golioth_ble_gatt_peripheral));
    if (NULL == peripheral)
    {
        goto finish;
    }

    peripheral->device_id = device_id;

    GOLIOTH_BLE_GATT_ATTR_ARRAY_LEN(&golioth_svc.attr_count);
    int err = bt_gatt_service_register(&golioth_svc);
    if (0 != err)
    {
        free(peripheral);
        peripheral = NULL;
    }

    STRUCT_SECTION_FOREACH(golioth_ble_gatt_characteristic, golioth_ble_gatt_chrc)
    {
        if (golioth_ble_gatt_chrc->init)
        {
            golioth_ble_gatt_chrc->init(peripheral, golioth_ble_gatt_chrc->attr);
        }
    }

finish:
    return peripheral;
}

int golioth_ble_gatt_peripheral_destroy(struct golioth_ble_gatt_peripheral *peripheral)
{
    free(peripheral);

    return 0;
}
