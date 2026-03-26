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
#include <pouch/transport/gatt/common/receiver.h>

#include <pouch/gateway/bt/connect.h>

#include <pouch/gateway/cert.h>

#include "cert.h"
#include "uplink.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(device_cert_gatt, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static void device_cert_cleanup(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->device_cert_ctx)
    {
        pouch_gateway_device_cert_abort(node->device_cert_ctx);
        node->device_cert_ctx = NULL;
    }

    if (node->device_cert_receiver)
    {
        pouch_gatt_receiver_destroy(node->device_cert_receiver);
        node->device_cert_receiver = NULL;
    }
}

static int device_cert_data_received_cb(void *conn,
                                        const void *data,
                                        size_t length,
                                        bool is_first,
                                        bool is_last)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    int err = pouch_gateway_device_cert_push(node->device_cert_ctx, data, length);
    if (err)
    {
        LOG_ERR("Failed to push device cert");
        goto finish;
    }

    if (is_last)
    {
        err = pouch_gateway_device_cert_finish(node->device_cert_ctx);
        if (err)
        {
            LOG_ERR("Failed to finish device cert: %d", err);
            goto finish;
        }
        node->device_cert_ctx = NULL;
    }

finish:
    if (err)
    {
        device_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
    }

    return err;
}

static int send_ack_cb(void *conn, const void *data, size_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);
    uint16_t handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT].value;

    return bt_gatt_write_without_response(conn, handle, data, length, false);
}

static uint8_t device_cert_notify_cb(struct bt_conn *conn,
                                     struct bt_gatt_subscribe_params *params,
                                     const void *data,
                                     uint16_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");
        device_cert_cleanup(conn);

        return BT_GATT_ITER_STOP;
    }

    if (NULL == node->device_cert_receiver)
    {
        enum pouch_gatt_ack_code code;
        if (pouch_gatt_packetizer_is_fin(data, length, &code))
        {
            LOG_WRN("Received FIN while idle: %d", code);
        }
        else
        {
            LOG_ERR("Received packet while idle");

            pouch_gatt_receiver_send_nack(send_ack_cb, conn, POUCH_GATT_NACK_IDLE);
        }

        return BT_GATT_ITER_STOP;
    }

    bool complete = false;
    int err = pouch_gatt_receiver_receive_data(node->device_cert_receiver, data, length, &complete);
    if (err)
    {
        LOG_ERR("Error receiving data: %d", err);

        device_cert_cleanup(conn);

        pouch_gateway_bt_finished(conn);

        return BT_GATT_ITER_STOP;
    }

    if (complete)
    {
        device_cert_cleanup(conn);

        pouch_gateway_uplink_start(conn);

        return BT_GATT_ITER_STOP;
    }


    return BT_GATT_ITER_CONTINUE;
}

void pouch_gateway_device_cert_read(struct bt_conn *conn)
{
    LOG_INF("Starting device cert read");

    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->device_cert_provisioned)
    {
        pouch_gateway_uplink_start(conn);
        return;
    }

    if (0 == node->attr_handles[POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT].ccc)
    {
        LOG_ERR("Did not discover Device Cert CCC");
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->device_cert_ctx = pouch_gateway_device_cert_start();
    if (node->device_cert_ctx == NULL)
    {
        LOG_ERR("Failed to allocate device cert context");
        device_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->device_cert_receiver =
        pouch_gatt_receiver_create(send_ack_cb,
                                   conn,
                                   device_cert_data_received_cb,
                                   conn,
                                   CONFIG_POUCH_GATT_DEVICE_CERT_WINDOW_SIZE);
    if (NULL == node->device_cert_receiver)
    {
        LOG_ERR("Failed to create receiver");
        device_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    struct bt_gatt_subscribe_params *subscribe_params = &node->device_cert_subscribe_params;
    memset(subscribe_params, 0, sizeof(*subscribe_params));

    subscribe_params->notify = device_cert_notify_cb;
    subscribe_params->value = BT_GATT_CCC_NOTIFY;
    subscribe_params->value_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT].value;
    subscribe_params->ccc_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT].ccc;
    atomic_set_bit(subscribe_params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
    int err = bt_gatt_subscribe(conn, subscribe_params);
    if (err)
    {
        LOG_ERR("BT subscribe request failed: %d", err);
        device_cert_cleanup(conn);
        pouch_gateway_bt_finished(conn);
    }
}
