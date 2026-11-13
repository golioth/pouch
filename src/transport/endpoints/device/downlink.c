/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/transport/downlink.h>
#include "endpoints.h"
#include "cert.h"

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
    if (IS_ENABLED(CONFIG_POUCH_ENCRYPTION_SAEAD) && !success)
    {
        /* We don't know the reason why downlink failed, but it could be because
         * the server didn't have our cert, so we'll send it up again next time
         * to be sure.
         */
        cert_uploaded(false);
    }

    pouch_downlink_finish();
}

const struct pouch_endpoint pouch_device_endpoint_downlink = {
    .start = start,
    .recv = recv,
    .end = end,
};
