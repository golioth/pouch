/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct pouch_bearer;

typedef int (*pouch_bearer_send_t)(struct pouch_bearer *bearer, const uint8_t *buf, size_t len);
typedef void (*pouch_bearer_ready_t)(struct pouch_bearer *bearer);
typedef void (*pouch_bearer_close_t)(struct pouch_bearer *bearer, bool success);

struct pouch_bearer
{
    pouch_bearer_send_t send;
    pouch_bearer_close_t close;
    pouch_bearer_ready_t ready;
    size_t maxlen;
    void *ctx;
};

static inline int pouch_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    return bearer->send(bearer, buf, len);
}

static inline void pouch_bearer_close(struct pouch_bearer *bearer, bool success)
{
    bearer->close(bearer, success);
}

/**
 * Notify the bearer that the endpoint is ready to proceed the transaction.
 * Used to nudge the bearer to get out of a waiting state.
 */
static inline void pouch_bearer_ready(struct pouch_bearer *bearer)
{
    bearer->ready(bearer);
}
