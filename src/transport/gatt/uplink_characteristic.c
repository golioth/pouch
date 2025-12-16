/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/uplink.h>
#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uplink_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

#define BT_ATT_OVERHEAD 3

static const struct bt_uuid_128 pouch_gatt_uplink_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_UPLINK_CHRC_VAL);

enum uplink_indicate_state
{
    UPLINK_INDICATE_IDLE,
    UPLINK_INDICATE_IN_PROGRESS,
    UPLINK_INDICATE_LAST,
    UPLINK_INDICATE_FINISHED,
};

static struct pouch_gatt_uplink_ctx
{
    struct pouch_gatt_packetizer *packetizer;
    struct bt_conn *conn;
    enum uplink_indicate_state state;
    struct bt_gatt_indicate_params indicate_params;
    size_t indicate_data_len;
} uplink_chrc_ctx;

static enum pouch_gatt_packetizer_result uplink_fill_cb(void *dst, size_t *dst_len, void *user_arg)
{
    struct pouch_uplink *pouch = user_arg;
    enum pouch_gatt_packetizer_result ret = POUCH_GATT_PACKETIZER_MORE_DATA;

    enum pouch_result pouch_ret = pouch_uplink_fill(pouch, dst, dst_len);

    if (POUCH_ERROR == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = POUCH_GATT_PACKETIZER_ERROR;
    }
    if (POUCH_NO_MORE_DATA == pouch_ret)
    {
        pouch_uplink_finish(pouch);
        ret = POUCH_GATT_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static struct pouch_gatt_packetizer *packetizer_init(void)
{
    struct pouch_uplink *pouch = pouch_uplink_start();
    if (NULL == pouch)
    {
        return NULL;
    }

    struct pouch_gatt_packetizer *packetizer;
    packetizer = pouch_gatt_packetizer_start_callback(uplink_fill_cb, pouch);

    if (NULL == packetizer)
    {
        pouch_uplink_finish(pouch);
    }

    return packetizer;
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

    struct pouch_gatt_uplink_ctx *ctx = attr->user_data;

    if (NULL == ctx->packetizer)
    {
        ctx->packetizer = packetizer_init();
        if (NULL == ctx->packetizer)
        {
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
    }

    size_t buf_len = len;
    enum pouch_gatt_packetizer_result ret =
        pouch_gatt_packetizer_get(ctx->packetizer, buf, &buf_len);

    if (POUCH_GATT_PACKETIZER_ERROR == ret)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == ret)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }

    return buf_len;
}

static void cleanup_context(struct pouch_gatt_uplink_ctx *ctx)
{
    if (NULL != ctx->packetizer)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }
    if (NULL != ctx->indicate_params.data)
    {
        free((void *) ctx->indicate_params.data);
        ctx->indicate_params.data = NULL;
        ctx->indicate_data_len = 0;
    }

    ctx->state = UPLINK_INDICATE_IDLE;
}

static void send_indication(struct pouch_gatt_uplink_ctx *ctx)
{
    if (NULL == ctx->packetizer)
    {
        return;
    }

    size_t buf_len = ctx->indicate_data_len;
    enum pouch_gatt_packetizer_result ret =
        pouch_gatt_packetizer_get(ctx->packetizer, (void *) ctx->indicate_params.data, &buf_len);

    if (POUCH_GATT_PACKETIZER_ERROR == ret)
    {
        ctx->state = UPLINK_INDICATE_FINISHED;
        return;
    }
    if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == ret)
    {
        ctx->state = UPLINK_INDICATE_LAST;
    }

    ctx->indicate_params.len = buf_len;

    bt_gatt_indicate(ctx->conn, &ctx->indicate_params);
}

static void uplink_indicate_cb(struct bt_conn *conn,
                               struct bt_gatt_indicate_params *params,
                               uint8_t err)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    /* If the indicate failed, or this is a response to the last packet */

    if (err || UPLINK_INDICATE_LAST == ctx->state)
    {
        ctx->state = UPLINK_INDICATE_FINISHED;
    }
}

static void uplink_indicate_destroy(struct bt_gatt_indicate_params *params)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    if (UPLINK_INDICATE_IN_PROGRESS == ctx->state)
    {
        send_indication(ctx);
    }
    else if (UPLINK_INDICATE_FINISHED == ctx->state)
    {
        cleanup_context(ctx);
    }
}

POUCH_GATT_CHARACTERISTIC(uplink,
                          (const struct bt_uuid *) &pouch_gatt_uplink_chrc_uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_INDICATE,
                          POUCH_GATT_PERM_READ,
                          uplink_read,
                          NULL,
                          &uplink_chrc_ctx);

static ssize_t uplink_ccc_write(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                uint16_t value)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    if (value & BT_GATT_CCC_INDICATE)
    {
        if (UPLINK_INDICATE_IDLE != ctx->state)
        {
            LOG_DBG("Indication already in progress");
            return sizeof(value);
        }

        /* Start sending */

        uint16_t mtu = bt_gatt_get_mtu(conn);
        if (mtu <= BT_ATT_OVERHEAD)
        {
            LOG_ERR("Invalid MTU, likely disconnected");
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->indicate_data_len = mtu - BT_ATT_OVERHEAD;
        ctx->indicate_params.data = malloc(ctx->indicate_data_len);
        if (NULL == ctx->indicate_params.data)
        {
            ctx->indicate_data_len = 0;
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->conn = conn;

        ctx->packetizer = packetizer_init();
        if (NULL == ctx->packetizer)
        {
            cleanup_context(ctx);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->indicate_params.attr = &uplink_chrc;
        ctx->indicate_params.func = uplink_indicate_cb;
        ctx->indicate_params.destroy = uplink_indicate_destroy;
        ctx->indicate_params.len = 0;

        ctx->state = UPLINK_INDICATE_IN_PROGRESS;
    }

    return sizeof(value);
}

static void uplink_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    if (value & BT_GATT_CCC_INDICATE)
    {
        LOG_DBG("Indications enabled");

        send_indication(ctx);
    }
    else
    {
        LOG_DBG("Indications disabled");

        /* Indications turned off, stop sending */

        cleanup_context(ctx);
    }
}

POUCH_GATT_CCC(uplink,
               uplink_ccc_changed,
               uplink_ccc_write,
               BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
