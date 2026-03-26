/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>

#include <pouch/gateway/info.h>
#include <pouch/gateway/bt/connect.h>

#include "cert.h"
#include "info.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(info_gatt, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static void info_cleanup(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->info_ctx)
    {
        pouch_gateway_info_abort(node->info_ctx);
        node->info_ctx = NULL;
    }

    if (node->info_receiver)
    {
        pouch_gatt_receiver_destroy(node->info_receiver);
        node->info_receiver = NULL;
    }
}

static int info_data_received_cb(void *conn,
                                 const void *data,
                                 size_t length,
                                 bool is_first,
                                 bool is_last)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    int err = pouch_gateway_info_push(node->info_ctx, data, length);
    if (err)
    {
        LOG_ERR("Failed to push info data: %d", err);
        return err;
    }

    if (is_last)
    {
        int err = pouch_gateway_info_finish(node->info_ctx,
                                            &node->server_cert_provisioned,
                                            &node->device_cert_provisioned);
        node->info_ctx = NULL;
        if (err)
        {
            LOG_ERR("Failed to parse info: %d", err);
            /* Continue anyway, as nothing contained in info is critical */
        }
    }

    return 0;
}

static int send_ack_cb(void *conn, const void *data, size_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);
    uint16_t handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_INFO].value;

    return bt_gatt_write_without_response(conn, handle, data, length, false);
}

static uint8_t info_notify_cb(struct bt_conn *conn,
                              struct bt_gatt_subscribe_params *params,
                              const void *data,
                              uint16_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");

        info_cleanup(conn);

        return BT_GATT_ITER_STOP;
    }

    if (NULL == node->info_receiver)
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
    int err = pouch_gatt_receiver_receive_data(node->info_receiver, data, length, &complete);
    if (err)
    {
        LOG_ERR("Error receiving data: %d", err);

        info_cleanup(conn);

        pouch_gateway_bt_finished(conn);

        return BT_GATT_ITER_STOP;
    }

    if (complete)
    {
        info_cleanup(conn);

        pouch_gateway_server_cert_write(conn);

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
    if (err)
    {
        LOG_ERR("CCC Write failed: %d", err);
    }
}

static void gateway_info_read_start(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    node->server_cert_provisioned = false;
    node->device_cert_provisioned = false;

    node->info_ctx = pouch_gateway_info_start();
    if (node->info_ctx == NULL)
    {
        LOG_ERR("Failed to start info read");
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->info_receiver = pouch_gatt_receiver_create(send_ack_cb,
                                                     conn,
                                                     info_data_received_cb,
                                                     conn,
                                                     CONFIG_POUCH_GATT_INFO_WINDOW_SIZE);
    if (NULL == node->info_receiver)
    {
        LOG_ERR("Failed to create receiver");
        info_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }

    struct bt_gatt_subscribe_params *subscribe_params = &node->info_subscribe_params;

    memset(subscribe_params, 0, sizeof(*subscribe_params));
    subscribe_params->notify = info_notify_cb;
    subscribe_params->subscribe = subscribe_cb;
    subscribe_params->value = BT_GATT_CCC_NOTIFY;
    subscribe_params->value_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_INFO].value;
    subscribe_params->ccc_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_INFO].ccc;
    atomic_set_bit(subscribe_params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
    int err = bt_gatt_subscribe(conn, subscribe_params);
    if (err)
    {
        LOG_ERR("BT subscribe request failed: %d", err);
        info_cleanup(conn);
        pouch_gateway_bt_finished(conn);
        return;
    }
}

void pouch_gateway_info_read_start(struct bt_conn *conn)
{
    gateway_info_read_start(conn);
}
