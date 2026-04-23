/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <pouch/transport/gatt/common/uuids.h>

#include "transport/sar/receiver.h"
#include "transport/sar/sender.h"
#include "transport/endpoints/endpoints.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_gatt, CONFIG_POUCH_GATT_LOG_LEVEL);

#define BT_ATT_OVERHEAD 3

#define CHAR_INIT_RECVEIVER(_endpoint)                                   \
    {                                                                    \
        .bearer =                                                        \
            {                                                            \
                .send = bearer_send,                                     \
                .abort = bearer_abort,                                   \
                .window = CONFIG_POUCH_TRANSPORT_GATT_WINDOW_SIZE,       \
            },                                                           \
        .type = CHAR_RECEIVER,                                           \
        .receiver = &((struct pouch_receiver){.endpoint = (_endpoint)}), \
    }
#define CHAR_INIT_SENDER(_endpoint)                                  \
    {                                                                \
        .bearer =                                                    \
            {                                                        \
                .send = bearer_send,                                 \
                .abort = bearer_abort,                               \
                .window = CONFIG_POUCH_TRANSPORT_GATT_WINDOW_SIZE,   \
            },                                                       \
        .type = CHAR_SENDER,                                         \
        .sender = &((struct pouch_sender){.endpoint = (_endpoint)}), \
    }

enum characteristic_type
{
    CHAR_RECEIVER,
    CHAR_SENDER,
};

struct pouch_characteristic
{
    struct pouch_bearer bearer;

    // session context:
    const struct bt_gatt_attr *attr;
    struct bt_conn *conn;

    // higher level handler:
    enum characteristic_type type;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    struct pouch_characteristic *c = CONTAINER_OF(bearer, struct pouch_characteristic, bearer);

    LOG_DBG("%p: tx: %u", c, len);
    LOG_HEXDUMP_DBG(buf, len, "tx");

    return bt_gatt_notify(c->conn, c->attr, buf, len);
}

static void bearer_abort(struct pouch_bearer *bearer)
{
    struct pouch_characteristic *c = CONTAINER_OF(bearer, struct pouch_characteristic, bearer);

    LOG_DBG("%p: abort", c);

    bt_conn_disconnect(c->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static struct pouch_characteristic *pouch_characteristic(const struct bt_gatt_attr *attr)
{
    return attr->user_data;
}

static int recv(const struct pouch_characteristic *c, const void *buf, size_t len)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_recv(c->receiver, buf, len);
        case CHAR_SENDER:
            return pouch_sender_recv(c->sender, buf, len);
    }

    return -EINVAL;
}

static int data_write(struct bt_conn *conn,
                      const struct bt_gatt_attr *attr,
                      const void *buf,
                      uint16_t len,
                      uint16_t offset,
                      uint8_t flags)
{
    const struct pouch_characteristic *c = pouch_characteristic(attr);
    LOG_DBG("%p: rx: %u", c, len);
    LOG_HEXDUMP_DBG(buf, len, "rx");
    int err = recv(c, buf, len);
    if (err)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return 0;
}

static int open(struct pouch_characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            return pouch_receiver_open(c->receiver, &c->bearer);
        case CHAR_SENDER:
            return pouch_sender_open(c->sender, &c->bearer);
    }

    return -ENOTSUP;
}

static int close(struct pouch_characteristic *c)
{
    switch (c->type)
    {
        case CHAR_RECEIVER:
            pouch_receiver_close(c->receiver);
            return 0;
        case CHAR_SENDER:
            pouch_sender_close(c->sender);
            return 0;
    }

    return -ENOTSUP;
}

static ssize_t ccc_write(struct bt_conn *conn, const struct bt_gatt_attr *ccc_attr, uint16_t value)
{
    const struct bt_gatt_attr *attr = ccc_attr - 1;
    struct pouch_characteristic *c = pouch_characteristic(attr);

    if (!(value & BT_GATT_CCC_NOTIFY))
    {
        return sizeof(value);
    }

    // NOTIFY enabled - the transport is open:
    uint16_t mtu = bt_gatt_get_mtu(conn);
    if (mtu <= BT_ATT_OVERHEAD)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    LOG_DBG("%p: open", c);

    c->conn = conn;
    c->attr = attr;
    c->bearer.maxlen = mtu - BT_ATT_OVERHEAD;

    int err = open(c);
    if (err)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return sizeof(value);
}

static void ccc_changed(const struct bt_gatt_attr *ccc_attr, uint16_t value)
{
    const struct bt_gatt_attr *attr = ccc_attr - 1;
    struct pouch_characteristic *c = pouch_characteristic(attr);
    if (value & BT_GATT_CCC_NOTIFY)
    {
        return;
    }

    LOG_DBG("%p: close", c);
    close(c);

    c->conn = NULL;
    c->attr = NULL;
}

/***************************************************
 * Pouch characteristics contexts:
 **************************************************/

static struct pouch_characteristic downlink = CHAR_INIT_RECVEIVER(&pouch_endpoint_downlink);
static struct pouch_characteristic uplink = CHAR_INIT_SENDER(&pouch_endpoint_uplink);
static struct pouch_characteristic info = CHAR_INIT_SENDER(&pouch_endpoint_info);

#if IS_ENABLED(CONFIG_POUCH_ENCRYPTION_SAEAD)
static struct pouch_characteristic server_cert = CHAR_INIT_RECVEIVER(&pouch_endpoint_server_cert);
static struct pouch_characteristic device_cert = CHAR_INIT_SENDER(&pouch_endpoint_device_cert);
#endif

#if IS_ENABLED(CONFIG_POUCH_TRANSPORT_GATT_PERM_AUTHEN)
#define CHAR_PERM BT_GATT_PERM_WRITE_AUTHEN
#else
#define CHAR_PERM BT_GATT_PERM_WRITE_LESC
#endif

#if IS_ENABLED(CONFIG_POUCH_TRANSPORT_GATT_CUD_ATTRIBUTES)

#define POUCH_CHARACTERISTIC(uuid, characteristic)                                                 \
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(uuid),                                              \
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,                               \
                           CHAR_PERM,                                                              \
                           NULL,                                                                   \
                           data_write,                                                             \
                           &characteristic),                                                       \
        BT_GATT_CCC_WITH_WRITE_CB(ccc_changed, ccc_write, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), \
        BT_GATT_CUD(#characteristic, BT_GATT_PERM_READ)

#else

#define POUCH_CHARACTERISTIC(uuid, characteristic)                   \
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(uuid),                \
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY, \
                           CHAR_PERM,                                \
                           NULL,                                     \
                           data_write,                               \
                           &characteristic),                         \
        BT_GATT_CCC_WITH_WRITE_CB(ccc_changed, ccc_write, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)

#endif


BT_GATT_SERVICE_DEFINE(pouch,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(POUCH_GATT_UUID_SVC_VAL_16)),

                       POUCH_CHARACTERISTIC(POUCH_GATT_UUID_DOWNLINK_CHRC_VAL, downlink),
                       POUCH_CHARACTERISTIC(POUCH_GATT_UUID_UPLINK_CHRC_VAL, uplink),
                       POUCH_CHARACTERISTIC(POUCH_GATT_UUID_INFO_CHRC_VAL, info),
#if IS_ENABLED(CONFIG_POUCH_ENCRYPTION_SAEAD)
                       POUCH_CHARACTERISTIC(POUCH_GATT_UUID_SERVER_CERT_CHRC_VAL, server_cert),
                       POUCH_CHARACTERISTIC(POUCH_GATT_UUID_DEVICE_CERT_CHRC_VAL, device_cert),
#endif
);
