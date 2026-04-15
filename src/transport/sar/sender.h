/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "../bearer.h"
#include <pouch/transport/types.h>
#include "../endpoints/endpoint.h"

struct pouch_sender
{
    const struct pouch_endpoint *endpoint;
    struct pouch_bearer *bearer;

    uint8_t *buf;

    uint8_t seq;
    uint8_t window;
    uint8_t state;
};

int pouch_sender_open(struct pouch_sender *sender, struct pouch_bearer *bearer);
void pouch_sender_ready(struct pouch_sender *sender);
int pouch_sender_recv(struct pouch_sender *sender, const uint8_t *buf, size_t len);
void pouch_sender_close(struct pouch_sender *sender);
