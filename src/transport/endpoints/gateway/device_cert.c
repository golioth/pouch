/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <pouch/gateway/cert.h>
#include "gateway/types.h"
#include "transport/bearer.h"
#include "endpoints.h"

static int start(struct pouch_bearer *bearer)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    node->device_cert_ctx = pouch_gateway_device_cert_start();
    if (!node->device_cert_ctx)
    {
        return -ENOMEM;
    }

    return 0;
}

static int recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    return pouch_gateway_device_cert_push(node->device_cert_ctx, buf, len);
}

static void end(struct pouch_bearer *bearer, bool success)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    if (!success)
    {
        pouch_gateway_device_cert_abort(node->device_cert_ctx);
        node->device_cert_ctx = NULL;
        return;
    }

    int err = pouch_gateway_device_cert_finish(node->device_cert_ctx);
    node->device_cert_ctx = NULL;
    if (err)
    {
        pouch_bearer_close(bearer, false);
        return;
    }

    node->device_cert_provisioned = true;
}

const struct pouch_endpoint gateway_endpoint_device_cert = {
    .start = start,
    .recv = recv,
    .end = end,
};
