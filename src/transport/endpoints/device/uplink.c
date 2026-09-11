/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/uplink.h>
#include "endpoints.h"


static int start(struct pouch_bearer *bearer)
{
    // ensure the uplink is started
    int err = pouch_uplink_start();
    if (err)
    {
        return err;
    }

    return pouch_uplink_pouch_open();
}

static enum pouch_result send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    return pouch_uplink_fill(dst, dst_len);
}

static void end(struct pouch_bearer *bearer, bool success)
{
    pouch_uplink_pouch_close();
}

const struct pouch_endpoint pouch_device_endpoint_uplink = {
    .start = start,
    .send = send,
    .end = end,
};
