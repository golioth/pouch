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
typedef void (*pouch_bearer_abort_t)(struct pouch_bearer *bearer);

struct pouch_bearer
{
    pouch_bearer_send_t send;
    pouch_bearer_abort_t abort;
    size_t maxlen;
    uint8_t window;
};

static inline int pouch_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    return bearer->send(bearer, buf, len);
}

static inline void pouch_bearer_abort(struct pouch_bearer *bearer)
{
    bearer->abort(bearer);
}
