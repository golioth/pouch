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

struct gatt_device;

enum pouch_gateway_gatt_attr
{
    POUCH_GATEWAY_GATT_ATTR_INFO,
    POUCH_GATEWAY_GATT_ATTR_DOWNLINK,
    POUCH_GATEWAY_GATT_ATTR_UPLINK,
    POUCH_GATEWAY_GATT_ATTR_SERVER_CERT,
    POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT,

    POUCH_GATEWAY_GATT_ATTRS,
};

enum characteristic_type
{
    CHAR_RECEIVER,
    CHAR_SENDER,
};

typedef void (*gateway_gatt_char_done_t)(struct gatt_device *device, enum pouch_gateway_gatt_attr);
typedef void (*gateway_gatt_discover_callback_t)(struct gatt_device *device, bool success);

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

    gateway_gatt_char_done_t callback;
    bool subscribed;
    enum characteristic_type type;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

/** Representation of a Pouch device exposing a BLE GATT Pouch service.  */
struct gatt_device
{
    struct characteristic chars[POUCH_GATEWAY_GATT_ATTRS];
    struct
    {
        struct bt_gatt_discover_params params;
        gateway_gatt_discover_callback_t callback;
    } discover;
    struct pouch_gateway_node_info node;
    struct bt_conn *conn;
    pouch_gateway_bt_end_t callback;
};
