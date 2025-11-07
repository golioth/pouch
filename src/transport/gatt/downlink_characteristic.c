/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/downlink.h>
#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>
#include <pouch/transport/gatt/common/uuids.h>

#include "pouch_gatt_declarations.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink_chrc, CONFIG_POUCH_GATT_LOG_LEVEL);

static const struct bt_uuid_128 pouch_gatt_downlink_chrc_uuid =
    BT_UUID_INIT_128(POUCH_GATT_UUID_DOWNLINK_CHRC_VAL);

struct bt_gatt_attr downlink_chrc;

struct pouch_gatt_receiver *receiver = NULL;

static int downlink_received_data_cb(void *unused,
                                     const void *data,
                                     size_t length,
                                     bool is_first,
                                     bool is_last)
{
    if (is_first)
    {
        pouch_downlink_start();
    }

    pouch_downlink_push(data, length);

    if (is_last)
    {
        pouch_downlink_finish();
    }

    return 0;
}

static int send_ack_cb(void *conn, const void *data, size_t length)
{
    return bt_gatt_notify(conn, &downlink_chrc, data, length);
}

static ssize_t downlink_write(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              const void *buf,
                              uint16_t len,
                              uint16_t offset,
                              uint8_t flags)
{
    if (!bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY))
    {

        LOG_WRN("Received downlink write but notifications disabled");

        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (NULL == receiver)
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
    int err = pouch_gatt_receiver_receive_data(receiver, buf, len, &complete);
    if (err)
    {
        LOG_ERR("Error receiving data: %d", err);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (complete)
    {
        pouch_gatt_receiver_destroy(receiver);
        receiver = NULL;
    }

    return len;
}

POUCH_GATT_CHARACTERISTIC(downlink,
                          (const struct bt_uuid *) &pouch_gatt_downlink_chrc_uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          POUCH_GATT_PERM_WRITE,
                          NULL,
                          downlink_write,
                          NULL);

static ssize_t downlink_ccc_write(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  uint16_t value)
{
    if (value & BT_GATT_CCC_NOTIFY)
    {
        receiver = pouch_gatt_receiver_create(send_ack_cb,
                                              conn,
                                              downlink_received_data_cb,
                                              NULL,
                                              CONFIG_POUCH_GATT_DOWNLINK_WINDOW_SIZE);
    }

    return sizeof(value);
}

static void downlink_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    if (value & BT_GATT_CCC_NOTIFY)
    {
        LOG_DBG("Notifications enabled");
    }
    else
    {
        LOG_DBG("Notifications disabled");
        if (receiver)
        {
            pouch_gatt_receiver_destroy(receiver);
            receiver = NULL;
        }
    }
}

POUCH_GATT_CCC(downlink,
               downlink_ccc_changed,
               downlink_ccc_write,
               BT_GATT_PERM_READ | BT_GATT_PERM_WRITE);
