/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
#include "../bearer.h"
#include "../endpoints/endpoint.h"

struct pouch_receiver
{
    const struct pouch_endpoint *endpoint;
    struct pouch_bearer *bearer;

    uint8_t seq;
    uint8_t ack;
    uint8_t window;
    uint8_t state;

    struct k_work_delayable work;
};

int pouch_receiver_open(struct pouch_receiver *recv, struct pouch_bearer *bearer, uint8_t window);
int pouch_receiver_recv(struct pouch_receiver *recv, const uint8_t *buf, size_t len);
void pouch_receiver_close(struct pouch_receiver *recv);
void pouch_receiver_ready(struct pouch_receiver *recv);
