/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pouch/gateway/cert.h>
#include "gateway/types.h"
#include "transport/bearer.h"
#include "endpoints.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(server_cert_endpoint, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static int start(struct pouch_bearer *bearer)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    node->server_cert_ctx = pouch_gateway_server_cert_start();
    if (node->server_cert_ctx == NULL)
    {
        LOG_ERR("Failed to start server cert process");
        return -ENOMEM;
    }

    return 0;
}

static enum pouch_result send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    bool last = false;
    int err = pouch_gateway_server_cert_get_data(node->server_cert_ctx, dst, dst_len, &last);
    if (err)
    {
        return POUCH_ERROR;
    }

    return last ? POUCH_NO_MORE_DATA : POUCH_MORE_DATA;
}

static void end(struct pouch_bearer *bearer, bool success)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    pouch_gateway_server_cert_abort(node->server_cert_ctx);
    node->server_cert_ctx = NULL;
    if (success)
    {
        node->server_cert_provisioned = true;
    }
}

const struct pouch_endpoint gateway_endpoint_server_cert = {
    .start = start,
    .send = send,
    .end = end,
};
