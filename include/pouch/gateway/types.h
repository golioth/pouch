/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/bluetooth/gatt.h>

#define POUCH_GATEWAY_BT_ATT_OVERHEAD 3 /* opcode (1) + handle (2) */

enum pouch_gateway_gatt_attr
{
    POUCH_GATEWAY_GATT_ATTR_INFO,
    POUCH_GATEWAY_GATT_ATTR_DOWNLINK,
    POUCH_GATEWAY_GATT_ATTR_UPLINK,
    POUCH_GATEWAY_GATT_ATTR_SERVER_CERT,
    POUCH_GATEWAY_GATT_ATTR_DEVICE_CERT,

    POUCH_GATEWAY_GATT_ATTRS,
};

enum server_cert_next_state
{
    SERVER_CERT_NEXT_DEVICE_CERT,
    SERVER_CERT_NEXT_SERVER_CERT,
    SERVER_CERT_NEXT_END
};

struct pouch_gateway_attr_handle
{
    uint16_t value;
    uint16_t ccc;
};

struct pouch_gateway_node_info
{
    struct pouch_gateway_attr_handle attr_handles[POUCH_GATEWAY_GATT_ATTRS];
    struct bt_gatt_discover_params discover_params;
    struct bt_gatt_subscribe_params info_subscribe_params;
    struct bt_gatt_subscribe_params server_cert_subscribe_params;
    struct bt_gatt_subscribe_params device_cert_subscribe_params;
    struct bt_gatt_subscribe_params uplink_subscribe_params;
    struct bt_gatt_subscribe_params downlink_subscribe_params;
    struct pouch_gateway_downlink_context *downlink_ctx;
    struct pouch_gatt_receiver *info_receiver;
    struct pouch_gatt_sender *server_cert_sender;
    struct pouch_gatt_receiver *device_cert_receiver;
    struct pouch_gatt_sender *downlink_sender;
    struct pouch_gatt_receiver *uplink_receiver;
    struct pouch_gatt_packetizer *packetizer;
    struct pouch_gateway_uplink *uplink;
    struct pouch_gateway_info_context *info_ctx;
    struct pouch_gateway_device_cert_context *device_cert_ctx;
    struct pouch_gateway_server_cert_context *server_cert_ctx;
    enum server_cert_next_state server_cert_next;
    bool server_cert_provisioned;
    bool device_cert_provisioned;
};
