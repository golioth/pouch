/*
 * Copyright (c) 2025 Golioth
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/uplink.h>

#include "toothfairy_declarations.h"
#include "toothfairy_uuids.h"
#include "packetizer.h"

static const struct bt_uuid_128 tf_info_chrc_uuid =
    BT_UUID_INIT_128(TF_UUID_GOLIOTH_UPLINK_CHRC_VAL);

static struct tf_uplink_ctx
{
    struct tf_packetizer *packetizer;
} uplink_chrc_ctx;

static enum tf_packetizer_result uplink_fill_cb(void *dst, size_t *dst_len, void *user_arg)
{
    struct pouch_uplink *pouch = user_arg;
    enum tf_packetizer_result ret = TF_PACKETIZER_MORE_DATA;

    enum pouch_result pouch_ret = pouch_uplink_fill(pouch, dst, dst_len);

    if (POUCH_ERROR == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = TF_PACKETIZER_ERROR;
    }
    if (POUCH_NO_MORE_DATA == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = TF_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static ssize_t uplink_read(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf,
                           uint16_t len,
                           uint16_t offset)
{
    /* Force TF packets into a single MTU */

    if (0 != offset)
    {
        return 0;
    }

    struct tf_uplink_ctx *ctx = attr->user_data;

    if (NULL == ctx->packetizer)
    {
        struct pouch_uplink *pouch = pouch_uplink_start();
        ctx->packetizer = tf_packetizer_start_callback(uplink_fill_cb, pouch);
    }

    size_t buf_len = len;
    enum tf_packetizer_result ret = tf_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (TF_PACKETIZER_ERROR == ret)
    {
        tf_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (TF_PACKETIZER_NO_MORE_DATA == ret)
    {
        tf_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

TOOTHFAIRY_CHARACTERISTIC(uplink,
                          (const struct bt_uuid *) &tf_info_chrc_uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ,
                          NULL,
                          uplink_read,
                          NULL,
                          &uplink_chrc_ctx);
