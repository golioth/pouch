/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <pouch/gateway/info.h>
#include <pouch/gateway/uplink.h>
#include <pouch/gateway/downlink.h>
#include <pouch/gateway/cert.h>

struct pouch_gateway_node_info
{
    struct pouch_gateway_downlink_context *downlink_ctx;
    struct pouch_gateway_uplink *uplink;
    struct pouch_gateway_info_context *info_ctx;
    struct pouch_gateway_device_cert_context *device_cert_ctx;
    struct pouch_gateway_server_cert_context *server_cert_ctx;
    bool server_cert_provisioned;
    bool device_cert_provisioned;
};
