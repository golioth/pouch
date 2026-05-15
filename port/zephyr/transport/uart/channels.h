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

struct channel
{
    enum serial_channel ch;
    struct pouch_bearer bearer;
    const struct pouch_endpoint *endpoint;
};

int serial_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len);
void serial_bearer_ready(struct pouch_bearer *bearer);
void serial_bearer_close(struct pouch_bearer *bearer, bool success);
