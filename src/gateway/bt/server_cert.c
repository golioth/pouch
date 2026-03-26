/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/sender.h>

#include <pouch/gateway/bt/connect.h>

#include <pouch/gateway/cert.h>

#include "cert.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(server_cert_gatt, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static void server_cert_cleanup(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->server_cert_ctx)
    {
        pouch_gateway_server_cert_abort(node->server_cert_ctx);
        node->server_cert_ctx = NULL;
    }

    if (node->packetizer)
    {
        pouch_gatt_packetizer_finish(node->packetizer);
        node->packetizer = NULL;
    }

    if (node->server_cert_sender)
    {
        pouch_gatt_sender_destroy(node->server_cert_sender);
        node->server_cert_sender = NULL;
    }
}

static enum pouch_gatt_packetizer_result server_cert_fill_cb(void *dst,
                                                             size_t *dst_len,
                                                             void *user_arg)
{
    bool last = false;

    int ret = pouch_gateway_server_cert_get_data(user_arg, dst, dst_len, &last);
    if (-EAGAIN == ret)
    {
        LOG_DBG("Awaiting additional %s data from cloud", "server cert");
        return POUCH_GATT_PACKETIZER_MORE_DATA;
    }
    if (ret < 0)
    {
        *dst_len = 0;
        return POUCH_GATT_PACKETIZER_ERROR;
    }

    return last ? POUCH_GATT_PACKETIZER_NO_MORE_DATA : POUCH_GATT_PACKETIZER_MORE_DATA;
}

static int send_data_cb(void *conn, const void *data, size_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    uint16_t server_cert_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_SERVER_CERT].value;

    int err = bt_gatt_write_without_response(conn, server_cert_handle, data, length, false);
    if (err)
    {
        /* This error gets propagated to server_cert_notify_cb via
           pouch_gatt_sender_receive_ack, so no cleanup required here */

        LOG_ERR("GATT write error: %d", err);
    }

    return err;
}

static uint8_t server_cert_notify_cb(struct bt_conn *conn,
                                     struct bt_gatt_subscribe_params *params,
                                     const void *data,
                                     uint16_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");

        server_cert_cleanup(conn);

        switch (node->server_cert_next)
        {
            case SERVER_CERT_NEXT_DEVICE_CERT:
                pouch_gateway_device_cert_read(conn);
                break;

            case SERVER_CERT_NEXT_SERVER_CERT:
                pouch_gateway_server_cert_write(conn);
                break;

            case SERVER_CERT_NEXT_END:
                pouch_gateway_bt_finished(conn);
                break;
        }

        node->server_cert_next = SERVER_CERT_NEXT_END;

        return BT_GATT_ITER_STOP;
    }

    if (NULL == node->server_cert_sender)
    {
        if (pouch_gatt_packetizer_is_ack(data, length))
        {
            LOG_DBG("Received ACK while idle");

            pouch_gatt_sender_send_fin(send_data_cb, conn, POUCH_GATT_NACK_IDLE);
        }
        else
        {
            /* Received NACK (or malformed packet) while idle. Do nothing */
            LOG_WRN("Received NACK while idle");
        }

        node->server_cert_next = SERVER_CERT_NEXT_END;

        return BT_GATT_ITER_STOP;
    }

    bool complete = false;
    int ret = pouch_gatt_sender_receive_ack(node->server_cert_sender, data, length, &complete);
    if (0 > ret)
    {
        LOG_ERR("Error handling ack: %d", ret);

        node->server_cert_next = SERVER_CERT_NEXT_END;

        return BT_GATT_ITER_STOP;
    }

    if (0 < ret)
    {
        LOG_WRN("Received NACK: %d", ret);

        node->server_cert_next = SERVER_CERT_NEXT_END;

        return BT_GATT_ITER_STOP;
    }

    if (complete)
    {
        LOG_DBG("Server cert complete");

        bool is_newest = pouch_gateway_server_cert_is_newest(node->server_cert_ctx);

        if (is_newest)
        {
            node->server_cert_next = SERVER_CERT_NEXT_DEVICE_CERT;
        }
        else
        {
            /* There was certificate update in the meantime, so send it again. */
            LOG_INF("Noticed certificate update, sending again");

            node->server_cert_next = SERVER_CERT_NEXT_SERVER_CERT;
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

void pouch_gateway_server_cert_write(struct bt_conn *conn)
{
    LOG_INF("Starting server cert write");

    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->server_cert_provisioned)
    {
        LOG_INF("Server cert already provisioned, skipping write");
        pouch_gateway_device_cert_read(conn);
        return;
    }

    if (0 == node->attr_handles[POUCH_GATEWAY_GATT_ATTR_SERVER_CERT].value)
    {
        LOG_ERR("%s characteristic undiscovered", "server cert");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->server_cert_ctx = pouch_gateway_server_cert_start();
    if (node->server_cert_ctx == NULL)
    {
        LOG_ERR("Failed to allocate server cert context");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->packetizer =
        pouch_gatt_packetizer_start_callback(server_cert_fill_cb, node->server_cert_ctx);
    if (node->packetizer == NULL)
    {
        LOG_ERR("Failed to start packetizer");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    size_t mtu = bt_gatt_get_mtu(conn);
    if (mtu < POUCH_GATEWAY_BT_ATT_OVERHEAD)
    {
        LOG_ERR("MTU too small");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }
    mtu -= POUCH_GATEWAY_BT_ATT_OVERHEAD;

    node->server_cert_sender = pouch_gatt_sender_create(node->packetizer, send_data_cb, conn, mtu);
    if (NULL == node->server_cert_sender)
    {
        LOG_ERR("Failed to create sender");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->server_cert_next = SERVER_CERT_NEXT_END;

    struct bt_gatt_subscribe_params *subscribe_params = &node->server_cert_subscribe_params;
    if (NULL == subscribe_params)
    {
        LOG_ERR("Could not subscribe to server cert characteristic");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    memset(subscribe_params, 0, sizeof(*subscribe_params));
    subscribe_params->notify = server_cert_notify_cb;
    subscribe_params->value = BT_GATT_CCC_NOTIFY;
    subscribe_params->value_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_SERVER_CERT].value;
    subscribe_params->ccc_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_SERVER_CERT].ccc;
    atomic_set_bit(subscribe_params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
    int err = bt_gatt_subscribe(conn, subscribe_params);
    if (err)
    {
        LOG_ERR("Could not subscribe to server cert characteristic");
        server_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
    }
}
