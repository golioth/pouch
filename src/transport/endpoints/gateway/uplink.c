/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/gateway/uplink.h>
#include "gateway/types.h"
#include "transport/bearer.h"
#include "endpoints.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uplink_endpoint, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static void uplink_end_cb(void *ctx, enum pouch_gateway_uplink_result res)
{
    struct pouch_bearer *bearer = ctx;
    struct pouch_gateway_node_info *node = bearer->ctx;
    node->uplink = NULL;

    if (POUCH_GATEWAY_UPLINK_SUCCESS != res)
    {
        pouch_bearer_close(bearer, false);
    }
}

static int start(struct pouch_bearer *bearer)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    if (node->downlink_ctx == NULL)
    {
        // todo: this dependency should be removed, this logic is too fragile
        LOG_ERR("Attempted to start uplink without downlink");
        return -EBUSY;
    }

    node->uplink = pouch_gateway_uplink_open(node->downlink_ctx, uplink_end_cb, bearer);
    if (node->uplink == NULL)
    {
        return -ENOMEM;
    }

    return 0;
}

static int recv(struct pouch_bearer *bearer, const void *payload, size_t len)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    return pouch_gateway_uplink_write(node->uplink, payload, len, false);
}

static void end(struct pouch_bearer *bearer, bool success)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    LOG_DBG("Uplink end: %s", success ? "success" : "fail");
    pouch_gateway_uplink_close(node->uplink);
    node->uplink = NULL;
}

const struct pouch_endpoint gateway_endpoint_uplink = {
    .start = start,
    .recv = recv,
    .end = end,
};
