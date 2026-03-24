/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct pouch_trx;

typedef int (*pouch_trx_send_t)(struct pouch_trx *trx, const uint8_t *buf, size_t len);
typedef void (*pouch_trx_abort_t)(struct pouch_trx *trx);

struct pouch_trx
{
    pouch_trx_send_t send;
    pouch_trx_abort_t abort;
    size_t maxlen;
    uint8_t window;
};

static inline int pouch_trx_send(struct pouch_trx *trx, const uint8_t *buf, size_t len)
{
    return trx->send(trx, buf, len);
}

static inline void pouch_trx_abort(struct pouch_trx *trx)
{
    trx->abort(trx);
}
