/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pouch/gateway/downlink.h>
#include "gateway/types.h"
#include "transport/bearer.h"
#include "transport/bearer.h"
#include "endpoints.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink_endpoint, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static void downlink_data_available(void *ctx)
{
    // Notify the bearer:
    struct pouch_bearer *bearer = ctx;
    pouch_bearer_ready(bearer);
}

static int start(struct pouch_bearer *bearer)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    node->downlink_ctx = pouch_gateway_downlink_open(downlink_data_available, bearer);
    if (node->downlink_ctx == NULL)
    {
        LOG_ERR("Failed to open downlink");
        return -ENOMEM;
    }

    return 0;
}

static enum pouch_result send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    bool last = false;
    int err = pouch_gateway_downlink_get_data(node->downlink_ctx, dst, dst_len, &last);
    if (err)
    {
        LOG_ERR("Error getting downlink data: %d", err);
        return POUCH_ERROR;
    }

    return last ? POUCH_NO_MORE_DATA : POUCH_MORE_DATA;
}

static void end(struct pouch_bearer *bearer, bool success)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    pouch_gateway_downlink_abort(node->downlink_ctx);
    node->downlink_ctx = NULL;
}

const struct pouch_endpoint gateway_endpoint_downlink = {
    .start = start,
    .send = send,
    .end = end,
};
