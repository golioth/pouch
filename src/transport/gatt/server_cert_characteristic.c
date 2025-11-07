/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/types.h>
#include <pouch/certificate.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(server_cert_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

static const struct bt_uuid_128 pouch_gatt_server_cert_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL);

static struct pouch_gatt_server_cert_ctx
{
    struct pouch_gatt_receiver *receiver;
    struct pouch_cert cert;
} server_cert_chrc_ctx;

struct bt_gatt_attr server_cert_chrc;

static int send_ack_cb(void *conn, const void *data, size_t length)
{
    bt_gatt_notify(conn, &server_cert_chrc, data, length);

    return 0;
}

static int server_cert_received_data_cb(void *arg,
                                        const void *data,
                                        size_t length,
                                        bool is_first,
                                        bool is_last)
{
    struct pouch_gatt_server_cert_ctx *ctx = arg;

    if (is_first)
    {
        if (!ctx->cert.buffer)
        {
            ctx->cert.buffer = malloc(CONFIG_POUCH_SERVER_CERT_MAX_LEN);
        }

        ctx->cert.size = 0;
    }

    if (!ctx->cert.buffer)
    {
        return -ENOMEM;
    }

    if (ctx->cert.size + length > CONFIG_POUCH_SERVER_CERT_MAX_LEN)
    {
        return -EFBIG;
    }

    memcpy((void *) &ctx->cert.buffer[ctx->cert.size], data, length);
    ctx->cert.size += length;

    if (is_last)
    {
        int err = pouch_server_certificate_set(&ctx->cert);
        if (err)
        {
            return -EINVAL;
        }

        free((void *) ctx->cert.buffer);
        ctx->cert.buffer = NULL;
    }

    return 0;
}

static ssize_t server_cert_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf,
                                 uint16_t len,
                                 uint16_t offset,
                                 uint8_t flags)
{
    if (!bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY))
    {
        LOG_WRN("Received server cert write but notifications disabled");

        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    struct pouch_gatt_server_cert_ctx *ctx = attr->user_data;

    if (NULL == ctx->receiver)
    {
        enum pouch_gatt_ack_code code;
        if (pouch_gatt_packetizer_is_fin(buf, len, &code))
        {
            LOG_WRN("Received FIN while idle: %d", code);
        }
        else
        {
            LOG_ERR("Received packet while idle");
            pouch_gatt_receiver_send_nack(send_ack_cb, conn, POUCH_GATT_NACK_IDLE);
        }

        return len;
    }

    bool complete = false;
    int err = pouch_gatt_receiver_receive_data(ctx->receiver, buf, len, &complete);
    if (err)
    {
        LOG_ERR("Error receiving data: %d", err);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(server_cert,
                          (const struct bt_uuid *) &pouch_gatt_server_cert_chrc_uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          POUCH_GATT_PERM_WRITE,
                          NULL,
                          server_cert_write,
                          &server_cert_chrc_ctx);

static ssize_t server_cert_ccc_write(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     uint16_t value)
{
    struct pouch_gatt_server_cert_ctx *ctx = &server_cert_chrc_ctx;

    if (value & BT_GATT_CCC_NOTIFY)
    {
        ctx->receiver = pouch_gatt_receiver_create(send_ack_cb,
                                                   conn,
                                                   server_cert_received_data_cb,
                                                   ctx,
                                                   CONFIG_POUCH_GATT_SERVER_CERT_WINDOW_SIZE);
    }

    return sizeof(value);
}

static void server_cert_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    struct pouch_gatt_server_cert_ctx *ctx = &server_cert_chrc_ctx;

    if (value & BT_GATT_CCC_NOTIFY)
    {
        LOG_DBG("Notifications enabled");
    }
    else
    {
        LOG_DBG("Notifications disabled");
        pouch_gatt_receiver_destroy(ctx->receiver);
        ctx->receiver = NULL;
        free((void *) ctx->cert.buffer);
        ctx->cert.buffer = NULL;
    }
}

POUCH_GATT_CCC(server_cert,
               server_cert_ccc_changed,
               server_cert_ccc_write,
               BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
