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

enum ch_flag
{
    CH_FLAG_OPEN,
    /** Opened, but no fragment has been sent or received yet. */
    CH_FLAG_OPENING,
    /** A frame should be produced on the next call to pouch_serial_ch_frame_get(). */
    CH_FLAG_PENDING,
    CH_FLAG_ERROR,
    CH_FLAG_SUSPENDED,
    /** Sender only: a fragment has been sent, and the remote hasn't ACKed it yet. */
    CH_FLAG_WAIT_ACK,
    /** Push sender only: the fragment waiting for an ACK is the last one. */
    CH_FLAG_LAST_SENT,
    CH_FLAG_COUNT,
};

struct pouch_serial_channel
{
    struct pouch_bearer bearer;
    const struct pouch_endpoint *endpoint;
    pouch_serial_ch_closed_cb_t closed_cb;
    POUCH_ATOMIC_DEFINE(flags, CH_FLAG_COUNT);
    uint8_t id;
};

int pouch_serial_ch_recv(struct pouch_serial_channel *ch,
                         const struct pouch_serial_header *header,
                         const void *payload,
                         size_t len);

size_t pouch_serial_ch_frame_get(struct pouch_serial_channel *ch, uint8_t *buf, size_t maxlen);

void pouch_serial_ch_ready(struct pouch_serial_channel *ch);
int pouch_serial_ch_open(struct pouch_serial_channel *ch);
void pouch_serial_ch_close(struct pouch_serial_channel *ch, bool success);
bool pouch_serial_ch_is_open(struct pouch_serial_channel *ch);
bool pouch_serial_ch_has_error(struct pouch_serial_channel *ch);

void pouch_serial_ch_suspend(struct pouch_serial_channel *ch, bool suspend);

static inline struct pouch_serial_channel *pouch_serial_ch_from_bearer(struct pouch_bearer *bearer)
{
    return CONTAINER_OF(bearer, struct pouch_serial_channel, bearer);
}
