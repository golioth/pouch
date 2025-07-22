/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/uplink.h>
#include <pouch/transport/ble_gatt/common/packetizer.h>
#include <pouch/transport/ble_gatt/common/uuids.h>

#include "golioth_ble_gatt_declarations.h"

static const struct bt_uuid_128 golioth_ble_gatt_uplink_chrc_uuid =
    BT_UUID_INIT_128(GOLIOTH_BLE_GATT_UUID_UPLINK_CHRC_VAL);

static struct golioth_ble_gatt_uplink_ctx
{
    struct golioth_ble_gatt_packetizer *packetizer;
} uplink_chrc_ctx;

static enum golioth_ble_gatt_packetizer_result uplink_fill_cb(void *dst,
                                                              size_t *dst_len,
                                                              void *user_arg)
{
    struct pouch_uplink *pouch = user_arg;
    enum golioth_ble_gatt_packetizer_result ret = GOLIOTH_BLE_GATT_PACKETIZER_MORE_DATA;

    enum pouch_result pouch_ret = pouch_uplink_fill(pouch, dst, dst_len);

    if (POUCH_ERROR == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = GOLIOTH_BLE_GATT_PACKETIZER_ERROR;
    }
    if (POUCH_NO_MORE_DATA == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static ssize_t uplink_read(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf,
                           uint16_t len,
                           uint16_t offset)
{
    /* Force packets into a single MTU */

    if (0 != offset)
    {
        return 0;
    }

    struct golioth_ble_gatt_uplink_ctx *ctx = attr->user_data;

    if (NULL == ctx->packetizer)
    {
        struct pouch_uplink *pouch = pouch_uplink_start();
        ctx->packetizer = golioth_ble_gatt_packetizer_start_callback(uplink_fill_cb, pouch);
    }

    size_t buf_len = len;
    enum golioth_ble_gatt_packetizer_result ret =
        golioth_ble_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (GOLIOTH_BLE_GATT_PACKETIZER_ERROR == ret)
    {
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA == ret)
    {
        golioth_ble_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

GOLIOTH_BLE_GATT_CHARACTERISTIC(uplink,
                                (const struct bt_uuid *) &golioth_ble_gatt_uplink_chrc_uuid,
                                BT_GATT_CHRC_READ,
                                BT_GATT_PERM_READ,
                                uplink_read,
                                NULL,
                                &uplink_chrc_ctx);
