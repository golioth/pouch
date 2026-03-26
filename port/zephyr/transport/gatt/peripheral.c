/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/gatt/peripheral.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

static const struct bt_uuid_16 pouch_gatt_svc_uuid = BT_UUID_INIT_16(POUCH_GATT_UUID_SVC_VAL_16);

POUCH_GATT_SERVICE(&pouch_gatt_svc_uuid);

static struct bt_gatt_service golioth_svc = {
    .attrs = POUCH_GATT_ATTR_ARRAY_PTR,
};

int pouch_gatt_peripheral_init(void)
{
    POUCH_GATT_ATTR_ARRAY_LEN(&golioth_svc.attr_count);

    return bt_gatt_service_register(&golioth_svc);
}
