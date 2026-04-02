/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "../trx.h"
#include <pouch/transport/types.h>

struct pouch_sender;


typedef int (*pouch_sender_start_t)(struct pouch_sender *s);
typedef void (*pouch_sender_end_t)(struct pouch_sender *s);
typedef enum pouch_result (*pouch_sender_fill_t)(struct pouch_sender *s,
                                                 void *dst,
                                                 size_t *dst_len);

struct pouch_sender_handler_api
{
    pouch_sender_start_t start;
    pouch_sender_end_t end;
    pouch_sender_fill_t fill;
};

struct pouch_sender
{
    const struct pouch_sender_handler_api *handler;
    struct pouch_trx *trx;

    uint8_t *buf;

    uint8_t seq;
    uint8_t window;
    uint8_t state;
};

int pouch_sender_open(struct pouch_sender *sender, struct pouch_trx *trx);
int pouch_sender_recv(struct pouch_sender *sender, const uint8_t *buf, size_t len);
void pouch_sender_close(struct pouch_sender *sender);
