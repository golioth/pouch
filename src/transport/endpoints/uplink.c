/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/uplink.h>
#include "endpoints.h"

static struct pouch_uplink *uplink;

static int start(void)
{
    uplink = pouch_uplink_start();
    if (uplink == NULL)
    {
        return -EAGAIN;
    }

    return 0;
}

static enum pouch_result send(void *dst, size_t *dst_len)
{
    if (uplink == NULL)
    {
        return POUCH_ERROR;
    }

    return pouch_uplink_fill(uplink, dst, dst_len);
}

static void end(bool success)
{
    pouch_uplink_finish(uplink);
    uplink = NULL;
}

const struct pouch_endpoint pouch_endpoint_uplink = {
    .start = start,
    .send = send,
    .end = end,
};
