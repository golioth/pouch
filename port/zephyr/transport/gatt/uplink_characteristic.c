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
#include <pouch/transport/gatt/common/sender.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uplink_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

#define BT_ATT_OVERHEAD 3

struct bt_gatt_attr uplink_chrc;

static const struct bt_uuid_128 pouch_gatt_uplink_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_UPLINK_CHRC_VAL);

static struct pouch_gatt_uplink_ctx
{
    struct pouch_uplink *pouch;
    struct pouch_gatt_packetizer *packetizer;
    struct pouch_gatt_sender *sender;
} uplink_chrc_ctx;

static void cleanup_context(struct pouch_gatt_uplink_ctx *ctx)
{
    if (ctx->packetizer)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }
    if (ctx->pouch)
    {
        pouch_uplink_finish(ctx->pouch);
        ctx->pouch = NULL;
    }
    if (ctx->sender)
    {
        pouch_gatt_sender_destroy(ctx->sender);
        ctx->sender = NULL;
    }
}

static enum pouch_gatt_packetizer_result uplink_fill_cb(void *dst, size_t *dst_len, void *user_arg)
{
    struct pouch_uplink *pouch = ((struct pouch_gatt_uplink_ctx *) user_arg)->pouch;
    enum pouch_gatt_packetizer_result ret = POUCH_GATT_PACKETIZER_MORE_DATA;

    enum pouch_result pouch_ret = pouch_uplink_fill(pouch, dst, dst_len);

    if (POUCH_ERROR == pouch_ret)
    {
        ret = POUCH_GATT_PACKETIZER_ERROR;
    }
    if (POUCH_NO_MORE_DATA == pouch_ret)
    {
        ret = POUCH_GATT_PACKETIZER_NO_MORE_DATA;
    }

    return ret;
}

static struct pouch_gatt_packetizer *packetizer_init(struct pouch_gatt_uplink_ctx *ctx)
{
    ctx->pouch = pouch_uplink_start();
    if (NULL == ctx->pouch)
    {
        LOG_ERR("Failed to create pouch");
        return NULL;
    }

    struct pouch_gatt_packetizer *packetizer;
    packetizer = pouch_gatt_packetizer_start_callback(uplink_fill_cb, ctx);

    if (NULL == packetizer)
    {
        LOG_ERR("Failed to create packetizer");
        pouch_uplink_finish(ctx->pouch);
        ctx->pouch = NULL;
    }

    return packetizer;
}

static int send_uplink_data(void *conn, const void *data, size_t length)
{
    return bt_gatt_notify(conn, &uplink_chrc, data, length);
}

static ssize_t uplink_write(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    if (NULL == ctx->sender)
    {
        if (pouch_gatt_packetizer_is_ack(buf, len))
        {
            LOG_DBG("Received ACK while idle");

            pouch_gatt_sender_send_fin(send_uplink_data, conn, POUCH_GATT_NACK_IDLE);
        }
        else
        {
            /* Received NACK (or malformed packet) while idle. Do nothing */
            LOG_WRN("Received NACK while idle");
        }

        return len;
    }

    bool complete = false;
    int ret = pouch_gatt_sender_receive_ack(ctx->sender, buf, len, &complete);
    if (0 > ret)
    {
        LOG_ERR("Error handling ACK: %d", ret);

        cleanup_context(ctx);

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);

        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (0 < ret)
    {
        LOG_WRN("Received NACK: %d", ret);

        cleanup_context(ctx);

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }

    if (complete)
    {
        LOG_DBG("Uplink complete");

        cleanup_context(ctx);
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(uplink,
                          (const struct bt_uuid *) &pouch_gatt_uplink_chrc_uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          POUCH_GATT_PERM_WRITE,
                          NULL,
                          uplink_write,
                          &uplink_chrc_ctx);

static ssize_t uplink_ccc_write(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                uint16_t value)
{
    struct pouch_gatt_uplink_ctx *ctx = &uplink_chrc_ctx;

    LOG_DBG("Uplink CCC write %d", value);

    if (value & BT_GATT_CCC_NOTIFY)
    {
        /* Start sending */

        uint16_t mtu = bt_gatt_get_mtu(conn);
        if (mtu <= BT_ATT_OVERHEAD)
        {
            LOG_ERR("Invalid MTU, likely disconnected");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        mtu -= BT_ATT_OVERHEAD;

        ctx->packetizer = packetizer_init(ctx);
        if (NULL == ctx->packetizer)
        {
            LOG_ERR("Failed to init packetizer");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->sender = pouch_gatt_sender_create(ctx->packetizer, send_uplink_data, conn, mtu);
        if (NULL == ctx->sender)
        {
            LOG_ERR("Failed to create sender");
            pouch_gatt_packetizer_finish(ctx->packetizer);
            ctx->packetizer = NULL;
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
    }

    return sizeof(value);
}

static void uplink_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    if (value & BT_GATT_CCC_NOTIFY)
    {
        LOG_DBG("Notifications enabled");
    }
    else
    {
        LOG_DBG("Notifications disabled");
        cleanup_context(&uplink_chrc_ctx);
    }
}

POUCH_GATT_CCC(uplink,
               uplink_ccc_changed,
               uplink_ccc_write,
               BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
