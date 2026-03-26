/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>

#include <pouch/gateway/types.h>
#include <pouch/gateway/uplink.h>
#include <pouch/gateway/bt/connect.h>

#include "downlink.h"
#include "uplink.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uplink_gatt, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

static int uplink_data_received_cb(void *conn,
                                   const void *data,
                                   size_t length,
                                   bool is_first,
                                   bool is_last)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (node->uplink == NULL)
    {
        return -ENOLINK;
    }

    int err = pouch_gateway_uplink_write(node->uplink, data, length, is_last);
    if (err)
    {
        LOG_ERR("Failed to write uplink data: %d", err);
        pouch_gateway_uplink_close(node->uplink);
        node->uplink = NULL;
    }
    else if (is_last)
    {
        pouch_gateway_uplink_close(node->uplink);
        node->uplink = NULL;
    }

    return err;
}

static int send_ack_cb(void *conn, const void *data, size_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);
    uint16_t handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_UPLINK].value;

    return bt_gatt_write_without_response_cb(conn, handle, data, length, false, NULL, NULL);
}

static uint8_t uplink_notify_cb(struct bt_conn *conn,
                                struct bt_gatt_subscribe_params *params,
                                const void *data,
                                uint16_t length)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");

        pouch_gatt_receiver_destroy(node->uplink_receiver);
        node->uplink_receiver = NULL;

        return BT_GATT_ITER_STOP;
    }

    if (NULL == node->uplink_receiver)
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

        return BT_GATT_ITER_CONTINUE;
    }

    bool complete = false;
    int err = pouch_gatt_receiver_receive_data(node->uplink_receiver, data, length, &complete);
    if (err)
    {
        LOG_ERR("Error receiving data: %d", err);
        pouch_gateway_bt_finished(conn);

        return BT_GATT_ITER_STOP;
    }

    if (complete)
    {
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static void uplink_end_cb(void *conn, enum pouch_gateway_uplink_result res)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);
    node->uplink = NULL;

    if (POUCH_GATEWAY_UPLINK_SUCCESS != res)
    {
        pouch_gateway_bt_finished(conn);
    }
}

void pouch_gateway_uplink_start(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    if (0 == node->attr_handles[POUCH_GATEWAY_GATT_ATTR_UPLINK].ccc)
    {
        LOG_ERR("No CCC for uplink");
        pouch_gateway_bt_finished(conn);
        return;
    }

    struct pouch_gateway_downlink_context *downlink = pouch_gateway_downlink_start(conn);
    if (downlink == NULL)
    {
        LOG_ERR("Failed to start downlink");
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->uplink = pouch_gateway_uplink_open(downlink, uplink_end_cb, conn);
    if (node->uplink == NULL)
    {
        LOG_ERR("Failed to open pouch uplink");
        pouch_gateway_bt_finished(conn);
        return;
    }

    node->uplink_receiver = pouch_gatt_receiver_create(send_ack_cb,
                                                       conn,
                                                       uplink_data_received_cb,
                                                       conn,
                                                       CONFIG_POUCH_GATT_UPLINK_WINDOW_SIZE);
    if (node->uplink_receiver == NULL)
    {
        LOG_ERR("Failed to create uplink sender");
        pouch_gateway_bt_finished(conn);
        return;
    }

    struct bt_gatt_subscribe_params *subscribe_params = &node->uplink_subscribe_params;
    memset(subscribe_params, 0, sizeof(*subscribe_params));

    subscribe_params->notify = uplink_notify_cb;
    subscribe_params->value = BT_GATT_CCC_NOTIFY;
    subscribe_params->value_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_UPLINK].value;
    subscribe_params->ccc_handle = node->attr_handles[POUCH_GATEWAY_GATT_ATTR_UPLINK].ccc;
    atomic_set_bit(subscribe_params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
    int err = bt_gatt_subscribe(conn, subscribe_params);
    if (err)
    {
        LOG_ERR("BT subscribe request failed: %d", err);
        pouch_gateway_bt_finished(conn);
    }
}

void pouch_gateway_uplink_cleanup(struct bt_conn *conn)
{
    struct pouch_gateway_node_info *node = pouch_gateway_get_node_info(conn);

    bt_gatt_unsubscribe(conn, &node->uplink_subscribe_params);

    if (node->uplink_receiver)
    {
        pouch_gatt_receiver_destroy(node->uplink_receiver);
        node->uplink_receiver = NULL;
    }

    if (node->uplink)
    {
        pouch_gateway_uplink_close(node->uplink);
        node->uplink = NULL;
    }
}
