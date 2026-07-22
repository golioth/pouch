/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <pouch/gateway/cert.h>
#include <pouch/port.h>
#include "gateway/types.h"
#include "gateway/uplink.h"
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

struct device_cert_finish_ctx
{
    struct pouch_gateway_device_cert_context *ctx;
    int result;
};

static void device_cert_finish_on_workq(void *arg)
{
    struct device_cert_finish_ctx *finish = arg;

    finish->result = pouch_gateway_device_cert_finish(finish->ctx);
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

    /*
     * pouch_gateway_device_cert_finish() performs a blocking CoAP/DTLS cloud upload (including a
     * potential DTLS handshake via mbedtls), which is far too stack-hungry to run on the transport
     * receive thread (e.g. the BT RX workqueue). Run it on the gateway work queue instead, blocking
     * here until it completes so the provisioning sequence semantics are preserved.
     */
    struct device_cert_finish_ctx finish = {
        .ctx = node->device_cert_ctx,
    };
    node->device_cert_ctx = NULL;

    pouch_gateway_workq_run_sync(device_cert_finish_on_workq, &finish);
    if (finish.result)
    {
        pouch_bearer_close(bearer, false);
        return;
    }

    node->device_cert_provisioned = true;
}

const struct pouch_endpoint broker_endpoint_device_cert = {
    .start = start,
    .recv = recv,
    .end = end,
};
