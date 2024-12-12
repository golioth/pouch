/*
 * Copyright (c) 2025 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "toothfairy_declarations.h"
#include "toothfairy_uuids.h"

static const struct bt_uuid_128 tf_info_chrc_uuid =
    BT_UUID_INIT_128(TF_UUID_GOLIOTH_UPLINK_CHRC_VAL);

static ssize_t uplink_read(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf,
                           uint16_t len,
                           uint16_t offset)
{
    return 0;
}

TOOTHFAIRY_CHARACTERISTIC(uplink,
                          (const struct bt_uuid *) &tf_info_chrc_uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          NULL,
                          uplink_read,
                          NULL,
                          NULL);
