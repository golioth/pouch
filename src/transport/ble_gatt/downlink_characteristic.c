/*
 * Copyright (c) 2025 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/downlink.h>
#include <pouch/transport/ble_gatt/common/packetizer.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

static const struct bt_uuid_128 golioth_ble_gatt_downlink_chrc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_DOWNLINK_CHRC_VAL);

ssize_t downlink_write(struct bt_conn *conn,
                       const struct bt_gatt_attr *attr,
                       const void *buf,
                       uint16_t len,
                       uint16_t offset,
                       uint8_t flags)
{
    bool is_first = false;
    bool is_last = false;
    const void *payload = NULL;
    ssize_t payload_len =
        golioth_ble_gatt_packetizer_decode(buf, len, &payload, &is_first, &is_last);

    if (0 >= payload_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (is_first)
    {
        pouch_downlink_start();
    }

    pouch_downlink_push(payload, payload_len);

    if (is_last)
    {
        pouch_downlink_finish();
    }

    return len;
}

GOLIOTH_BLE_GATT_CHARACTERISTIC(downlink,
                                (const struct bt_uuid *) &golioth_ble_gatt_downlink_chrc_uuid,
                                BT_GATT_CHRC_WRITE,
                                BT_GATT_PERM_WRITE,
                                NULL,
                                NULL,
                                downlink_write,
                                NULL);
