/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "transport/bearer.h"
#include "gateway/types.h"
#include <pouch/gateway/bt/connect.h>
#include <zephyr/bluetooth/gatt.h>

struct broker_bt_gatt_device;

enum broker_bt_attr
{
    BROKER_BT_ATTR_INFO,
    BROKER_BT_ATTR_DOWNLINK,
    BROKER_BT_ATTR_UPLINK,
    BROKER_BT_ATTR_SERVER_CERT,
    BROKER_BT_ATTR_DEVICE_CERT,

    BROKER_BT_ATTRS,
};

enum characteristic_type
{
    CHAR_RECEIVER,
    CHAR_SENDER,
};

typedef void (*broker_bt_gatt_char_done_t)(struct broker_bt_gatt_device *device,
                                           enum broker_bt_attr attr);
typedef void (*broker_bt_gatt_discover_callback_t)(struct broker_bt_gatt_device *device,
                                                   bool success);

/** BLE GATT Pouch characteristic, as seen from the GATT client's side */
struct characteristic
{
    struct pouch_bearer bearer;

    struct bt_gatt_subscribe_params sub_params;

    struct
    {
        uint16_t value;
        uint16_t ccc;
    } handle;

    broker_bt_gatt_char_done_t callback;
    bool subscribed;
    enum characteristic_type type;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

/** Representation of a Pouch device exposing a BLE GATT Pouch service.  */
struct broker_bt_gatt_device
{
    struct characteristic chars[BROKER_BT_ATTRS];
    struct
    {
        struct bt_gatt_discover_params params;
        broker_bt_gatt_discover_callback_t callback;
    } discover;
    struct pouch_gateway_node_info node;
    struct bt_conn *conn;
    pouch_gateway_bt_end_t callback;
};
