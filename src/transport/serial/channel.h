/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../bearer.h"
#include "packet.h"

#include <pouch/port.h>

struct pouch_serial_channel;
struct pouch_endpoint;

typedef void (*pouch_serial_ch_closed_cb_t)(struct pouch_serial_channel *ch, bool success);

struct pouch_serial_channel
{
    struct pouch_bearer bearer;
    const struct pouch_endpoint *endpoint;
    pouch_serial_ch_closed_cb_t closed_cb;
    pouch_atomic_t flags;
    uint8_t id;
};

int pouch_serial_ch_recv(struct pouch_serial_channel *ch,
                         const struct pouch_serial_header *header,
                         const void *payload,
                         size_t len);

size_t pouch_serial_ch_frame_get(struct pouch_serial_channel *ch, uint8_t *buf, size_t maxlen);

void pouch_serial_ch_ready(struct pouch_serial_channel *ch);
void pouch_serial_ch_close(struct pouch_serial_channel *ch, bool success);
bool pouch_serial_ch_is_open(struct pouch_serial_channel *ch);
bool pouch_serial_ch_has_error(struct pouch_serial_channel *ch);

static inline struct pouch_serial_channel *pouch_serial_ch_from_bearer(struct pouch_bearer *bearer)
{
    return CONTAINER_OF(bearer, struct pouch_serial_channel, bearer);
}
