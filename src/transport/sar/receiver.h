/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
#include "../trx.h"

struct pouch_receiver;

typedef int (*pouch_receiver_start_t)(struct pouch_receiver *r);
typedef void (*pouch_receiver_end_t)(struct pouch_receiver *r, bool success);
typedef int (*pouch_receiver_recv_t)(struct pouch_receiver *r, const void *buf, size_t len);

struct pouch_receiver_handler_api
{
    pouch_receiver_start_t start;
    pouch_receiver_end_t end;
    pouch_receiver_recv_t recv;
};

struct pouch_receiver
{
    const struct pouch_receiver_handler_api *handler;
    struct pouch_trx *trx;

    uint8_t seq;
    uint8_t ack;
    uint8_t state;

    struct k_work_delayable work;
};

int pouch_receiver_open(struct pouch_receiver *recv, struct pouch_trx *trx);
int pouch_receiver_recv(struct pouch_receiver *recv, const uint8_t *buf, size_t len);
void pouch_receiver_close(struct pouch_receiver *recv);
