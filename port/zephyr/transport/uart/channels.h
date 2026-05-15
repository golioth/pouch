/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <zephyr/sys/util.h>
#include "transport/bearer.h"
#include "protocol.h"

#define CHANNEL_OF(_bearer) (CONTAINER_OF(_bearer, struct channel, bearer))

#define CHANNEL_RECV(_endpoint)          \
    {.dir = SERIAL_CHANNEL_DIR_RECEIVER, \
     .receiver = &((struct pouch_receiver) {.endpoint = _endpoint})}
#define CHANNEL_SENDER(_endpoint) \
    {.dir = SERIAL_CHANNEL_DIR_SENDER, .sender = &((struct pouch_sender) {.endpoint = _endpoint})}

enum channel_dir
{
    SERIAL_CHANNEL_DIR_SENDER,
    SERIAL_CHANNEL_DIR_RECEIVER,
};

struct channel
{
    struct pouch_bearer bearer;
    enum serial_channel ch;
    enum channel_dir dir;
    union
    {
        struct pouch_sender *sender;
        struct pouch_receiver *receiver;
    };
};

int serial_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len);
void serial_bearer_ready(struct pouch_bearer *bearer);
void serial_bearer_close(struct pouch_bearer *bearer, bool success);
