/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "types.h"
#include "discover.h"
#include "../common.h"
#include "transport/sar/receiver.h"
#include "transport/sar/sender.h"
#include "transport/endpoints/gateway/endpoints.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gateway, CONFIG_POUCH_GATEWAY_GATT_LOG_LEVEL);

#define RECEIVER(_endpoint)                    \
    {                                          \
        .type = CHAR_RECEIVER,                 \
        .receiver = &((struct pouch_receiver){ \
            .endpoint = (_endpoint),           \
        }),                                    \
    }
#define SENDER(_endpoint)                  \
    {                                      \
        .type = CHAR_SENDER,               \
        .sender = &((struct pouch_sender){ \
            .endpoint = (_endpoint),       \
        }),                                \
    }

static struct gatt_device devices[CONFIG_BT_MAX_CONN] = {
    [0 ...(CONFIG_BT_MAX_CONN - 1)] =
        {
            .chars =
                {
                    [POUCH_GATEWAY_GATT_ATTR_INFO] = RECEIVER(&gateway_endpoint_info),
                    [POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT] = RECEIVER(&gateway_endpoint_device_cert),
                    [POUCH_GATEWAY_GATT_ATTR_SERVER_CERT] = SENDER(&gateway_endpoint_server_cert),
                    [POUCH_GATEWAY_GATT_ATTR_UPLINK] = RECEIVER(&gateway_endpoint_uplink),
                    [POUCH_GATEWAY_GATT_ATTR_DOWNLINK] = SENDER(&gateway_endpoint_downlink),
                },
        },
};

static void discover_complete(struct gatt_device *device, bool success);
static void sub_complete(struct gatt_device *device, enum pouch_gateway_gatt_attr attr);
static void link_complete(struct gatt_device *device, enum pouch_gateway_gatt_attr attr);
static void close_subscriptions(struct gatt_device *device);

static inline struct gatt_device *device_from_characteristic(struct characteristic *c)
{
    ptrdiff_t diff = (intptr_t) c - (intptr_t) &devices[0];
    size_t index = diff / sizeof(devices[0]);  // rounds down
    if (index < CONFIG_BT_MAX_CONN)
    {
        return &devices[index];
    }

    return NULL;
}

struct gatt_device *gateway_gatt_device(struct bt_conn *conn)
{
    return &devices[bt_conn_index(conn)];
}

static void finish(struct gatt_device *device)
{
    close_subscriptions(device);

    if (device->callback && device->conn)
    {
        device->callback(device->conn);
    }
}

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    struct gatt_device *device = device_from_characteristic(c);
    if (device == NULL)
    {
        return -ENOENT;
    }

    return bt_gatt_write_without_response(device->conn, c->handle.value, buf, len, false);
}

static void bearer_close(struct pouch_bearer *bearer, bool success)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    struct gatt_device *device = device_from_characteristic(c);
    if (device == NULL)
    {
        LOG_ERR("Unknown bearer %p", bearer);
        return;
    }

    LOG_DBG("%p: %s", c, success ? "success" : "fail");
    c->subscribed = false;
    if (!success)
    {
        finish(device);
    }
}

static void bearer_ready(struct pouch_bearer *bearer)
{
    struct characteristic *c = CONTAINER_OF(bearer, struct characteristic, bearer);
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_ready(c->receiver);
        case CHAR_SENDER:
            return pouch_sender_ready(c->sender);
    }
}

static int open(struct characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_open(c->receiver, &c->bearer, CONFIG_POUCH_GATT_WINDOW_SIZE);
        case CHAR_SENDER:
            return pouch_sender_open(c->sender, &c->bearer);
    }
    return -EINVAL;
}

static void close(struct characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            pouch_receiver_close(c->receiver);
            break;
        case CHAR_SENDER:
            pouch_sender_close(c->sender);
            break;
    }
}

static int recv(struct characteristic *c, const void *data, size_t length)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_recv(c->receiver, data, length);
        case CHAR_SENDER:
            return pouch_sender_recv(c->sender, data, length);
    }
    return -EINVAL;
}

static uint8_t notify_cb(struct bt_conn *conn,
                         struct bt_gatt_subscribe_params *params,
                         const void *data,
                         uint16_t length)
{
    struct gatt_device *device = gateway_gatt_device(conn);
    struct characteristic *c = CONTAINER_OF(params, struct characteristic, sub_params);

    if (NULL == data)
    {
        LOG_DBG("Subscription terminated");
        close(c);
        c->callback(device, (enum pouch_gateway_gatt_attr)(c - device->chars));
        return BT_GATT_ITER_STOP;
    }

    int err = recv(c, data, length);
    if (err)
    {
        LOG_ERR("Recv failed: %d", err);
        finish(device);
        return BT_GATT_ITER_STOP;
    }

    if (!c->subscribed)
    {
        LOG_DBG("Unsubscribing");
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static int subscribe(struct gatt_device *device,
                     enum pouch_gateway_gatt_attr attr,
                     gateway_gatt_char_done_t callback)
{
    uint16_t mtu = bt_gatt_get_mtu(device->conn);
    if (mtu <= BT_ATT_OVERHEAD)
    {
        return -EIO;
    }

    struct characteristic *c = &device->chars[attr];

    c->bearer.close = bearer_close;
    c->bearer.ready = bearer_ready;
    c->bearer.send = bearer_send;
    c->bearer.ctx = &device->node;
    c->bearer.maxlen = mtu - BT_ATT_OVERHEAD;

    struct bt_gatt_subscribe_params params = {
        .notify = notify_cb,
        .value = BT_GATT_CCC_NOTIFY,
        .value_handle = c->handle.value,
        .ccc_handle = c->handle.ccc,
    };
    atomic_set_bit(params.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

    c->sub_params = params;
    c->subscribed = true;
    c->callback = callback;

    int err = bt_gatt_subscribe(device->conn, &c->sub_params);
    if (err)
    {
        c->subscribed = false;
        return err;
    }

    err = open(c);
    if (err)
    {
        LOG_ERR("Failed to open bearer");
        (void) bt_gatt_unsubscribe(device->conn, &c->sub_params);
        return err;
    }

    return 0;
}

static void close_subscriptions(struct gatt_device *device)
{
    for (enum pouch_gateway_gatt_attr i = 0; i < POUCH_GATEWAY_GATT_ATTRS; i++)
    {
        struct characteristic *c = &device->chars[i];
        if (c->subscribed)
        {
            close(c);
            c->callback(device, i);
            c->subscribed = false;
        }
    }
}

static void on_disconnect(struct bt_conn *conn, uint8_t reason)
{
    struct gatt_device *device = gateway_gatt_device(conn);

    // If the device disconnects abruptly, the BLE stack won't clean up our GATT subscriptions:
    close_subscriptions(device);

    device->conn = NULL;
}

BT_CONN_CB_DEFINE(gateway_conn_listener) = {
    .disconnected = on_disconnect,
};

int pouch_gateway_bt_start(struct bt_conn *conn, pouch_gateway_bt_end_t callback)
{
    struct gatt_device *device = gateway_gatt_device(conn);
    int err;

    device->conn = conn;
    device->callback = callback;

    // TODO: Could skip this if we've already ran discovery on this device:
    err = gateway_bt_discover(device, discover_complete);
    if (err)
    {
        LOG_ERR("device %d: ERR: %d", device - &devices[0], err);
        finish(device);
        return err;
    }

    return 0;
}

static void discover_complete(struct gatt_device *device, bool success)
{
    if (!success)
    {
        finish(device);
        return;
    }

    // Start by reading the info endpoint:
    int err = subscribe(device, POUCH_GATEWAY_GATT_ATTR_INFO, sub_complete);
    if (err)
    {
        finish(device);
        return;
    }
}

static void sub_complete(struct gatt_device *device, enum pouch_gateway_gatt_attr attr)
{
    int err;

    if (!device->node.server_cert_provisioned)
    {
        if (attr == POUCH_GATEWAY_GATT_ATTR_SERVER_CERT)
        {
            LOG_ERR("Failed to provision server cert");
            finish(device);
            return;
        }

        err = subscribe(device, POUCH_GATEWAY_GATT_ATTR_SERVER_CERT, sub_complete);
        if (err)
        {
            finish(device);
            return;
        }

        return;
    }

    if (!device->node.device_cert_provisioned)
    {
        if (attr == POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT)
        {
            LOG_ERR("Failed to provision device cert");
            finish(device);
            return;
        }

        err = subscribe(device, POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT, sub_complete);
        if (err)
        {
            finish(device);
            return;
        }

        return;
    }

    // start link:
    err = subscribe(device, POUCH_GATEWAY_GATT_ATTR_DOWNLINK, link_complete);
    if (err)
    {
        finish(device);
        return;
    }
    err = subscribe(device, POUCH_GATEWAY_GATT_ATTR_UPLINK, link_complete);
    if (err)
    {
        finish(device);
        return;
    }
}

static void link_complete(struct gatt_device *device, enum pouch_gateway_gatt_attr attr)
{
    if (device->chars[POUCH_GATEWAY_GATT_ATTR_DOWNLINK].subscribed
        || device->chars[POUCH_GATEWAY_GATT_ATTR_UPLINK].subscribed)
    {
        // wait for both uplink and downlink to finish
        return;
    }

    finish(device);
}
