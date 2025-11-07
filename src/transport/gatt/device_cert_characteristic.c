/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/types.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/sender.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(device_cert_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

#define BT_ATT_OVERHEAD 3

static const struct bt_uuid_128 pouch_gatt_device_cert_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL);

static struct pouch_gatt_device_cert_ctx
{
    struct pouch_gatt_packetizer *packetizer;
    struct pouch_gatt_sender *sender;
} device_cert_chrc_ctx;

struct bt_gatt_attr device_cert_chrc;

static void cleanup_context(struct pouch_gatt_device_cert_ctx *ctx)
{
    if (ctx->packetizer)
    {
        pouch_gatt_packetizer_finish(ctx->packetizer);
        ctx->packetizer = NULL;
    }
    if (ctx->sender)
    {
        pouch_gatt_sender_destroy(ctx->sender);
        ctx->sender = NULL;
    }
}

static int send_device_cert_data(void *conn, const void *data, size_t length)
{
    return bt_gatt_notify(conn, &device_cert_chrc, data, length);
}

static ssize_t device_cert_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf,
                                 uint16_t len,
                                 uint16_t offset,
                                 uint8_t flags)
{
    struct pouch_gatt_device_cert_ctx *ctx = &device_cert_chrc_ctx;

    if (!ctx->sender)
    {
        if (pouch_gatt_packetizer_is_ack(buf, len))
        {
            LOG_DBG("Received ACK while idle");

            pouch_gatt_sender_send_fin(send_device_cert_data, conn, POUCH_GATT_NACK_IDLE);
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
        LOG_DBG("Device Cert upload complete");

        cleanup_context(ctx);
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(device_cert,
                          (const struct bt_uuid *) &pouch_gatt_device_cert_chrc_uuid,
                          BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
                          POUCH_GATT_PERM_WRITE,
                          NULL,
                          device_cert_write,
                          &device_cert_chrc_ctx);

static ssize_t device_cert_ccc_write(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     uint16_t value)
{
    struct pouch_gatt_device_cert_ctx *ctx = &device_cert_chrc_ctx;

    if (value & BT_GATT_CCC_NOTIFY)
    {
        /* Prepare to send */

        uint16_t mtu = bt_gatt_get_mtu(conn);
        if (mtu <= BT_ATT_OVERHEAD)
        {
            LOG_ERR("Invalid MTU, likely disconnected");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        mtu -= BT_ATT_OVERHEAD;

        struct pouch_cert cert = {0};

        int err = pouch_device_certificate_get(&cert);
        if (err)
        {
            LOG_ERR("Couldn't get device certificate: %d", err);
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->packetizer = pouch_gatt_packetizer_start_buffer(cert.buffer, cert.size);
        if (!ctx->packetizer)
        {
            LOG_ERR("Could not create packetizer");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }

        ctx->sender = pouch_gatt_sender_create(ctx->packetizer, send_device_cert_data, conn, mtu);
        if (!ctx->sender)
        {
            LOG_ERR("Could not create sender");
            pouch_gatt_packetizer_finish(ctx->packetizer);
            ctx->packetizer = NULL;
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
    }

    return sizeof(value);
}

static void device_cert_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    if (value & BT_GATT_CCC_NOTIFY)
    {
        LOG_DBG("Notifications enabled");
    }
    else
    {
        LOG_DBG("Notifications disabled");
        cleanup_context(&device_cert_chrc_ctx);
    }
}

POUCH_GATT_CCC(device_cert,
               device_cert_ccc_changed,
               device_cert_ccc_write,
               BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
