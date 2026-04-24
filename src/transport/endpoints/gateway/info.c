/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <pouch/gateway/info.h>
#include "gateway/types.h"
#include "transport/bearer.h"
#include "endpoints.h"

static int start(struct pouch_bearer *bearer)
{
    struct pouch_gateway_node_info *node = bearer->ctx;
    node->info_ctx = pouch_gateway_info_start();
    if (!node->info_ctx)
    {
        return -ENOMEM;
    }

    return 0;
}

static int recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    return pouch_gateway_info_push(node->info_ctx, buf, len);
}

static void end(struct pouch_bearer *bearer, bool success)
{
    struct pouch_gateway_node_info *node = bearer->ctx;

    if (!success)
    {
        pouch_gateway_info_abort(node->info_ctx);
        node->info_ctx = NULL;
        return;
    }

    int err = pouch_gateway_info_finish(node->info_ctx,
                                        &node->server_cert_provisioned,
                                        &node->device_cert_provisioned);
    if (err)
    {
        pouch_bearer_close(bearer, false);
    }

    node->info_ctx = NULL;
}

const struct pouch_endpoint gateway_endpoint_info = {
    .start = start,
    .recv = recv,
    .end = end,
};
