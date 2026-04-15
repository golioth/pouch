/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/transport/downlink.h>
#include "endpoints.h"

static int start(struct pouch_bearer *bearer)
{
    pouch_downlink_start();
    return 0;
}

static int recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    return pouch_downlink_push(buf, len);
}

static void end(struct pouch_bearer *bearer, bool success)
{
    pouch_downlink_finish();
}

const struct pouch_endpoint pouch_endpoint_downlink = {
    .start = start,
    .recv = recv,
    .end = end,
};
