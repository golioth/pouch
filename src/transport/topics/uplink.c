/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pouch/transport/certificate.h>
#include <pouch/transport/uplink.h>
#include "sar/sender.h"

static struct pouch_uplink *uplink;

static int start(struct pouch_sender *s)
{
    uplink = pouch_uplink_start();
    if (uplink == NULL)
    {
        return -EAGAIN;
    }

    return 0;
}

static enum pouch_result fill(struct pouch_sender *s, void *dst, size_t *dst_len)
{
    if (uplink == NULL)
    {
        return POUCH_ERROR;
    }

    return pouch_uplink_fill(uplink, dst, dst_len);
}

static void end(struct pouch_sender *s)
{
    pouch_uplink_finish(uplink);
    uplink = NULL;
}

static const struct pouch_sender_handler_api api = {
    .start = start,
    .fill = fill,
    .end = end,
};

struct pouch_sender pouch_topic_uplink = {
    .handler = &api,
};
