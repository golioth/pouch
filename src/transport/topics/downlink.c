/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <errno.h>

#include <pouch/transport/downlink.h>
#include "sar/receiver.h"

static int start(struct pouch_receiver *receiver)
{
    pouch_downlink_start();
    return 0;
}

static int recv(struct pouch_receiver *r, const void *buf, size_t len)
{
    return pouch_downlink_push(buf, len);
}

static void end(struct pouch_receiver *receiver, bool success)
{
    pouch_downlink_finish();
}

static const struct pouch_receiver_handler_api api = {
    .start = start,
    .recv = recv,
    .end = end,
};

struct pouch_receiver pouch_topic_downlink = {
    .handler = &api,
};
